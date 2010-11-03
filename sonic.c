/* Sonic library
   Copyright 2010
   Bill Cox
   This file is part of the Sonic Library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sonic.h"

struct sonicStreamStruct {
    double speed;
    float *inputBuffer;
    float *outputBuffer;
    int inputBufferSize;
    int outputBufferSize;
    int numInputSamples;
    int numOutputSamples;
    int minPeriod;
    int maxPeriod;
    int maxRequired;
    int remainingInputToCopy;
    int sampleRate;
};

/* Destroy the sonic stream. */
void sonicDestroyStream(
    sonicStream stream)
{
    if(stream->inputBuffer != NULL) {
	free(stream->inputBuffer);
    }
    if(stream->outputBuffer != NULL) {
	free(stream->outputBuffer);
    }
    free(stream);
}

/* Create a sonic stream.  Return NULL only if we are out of memory and cannot
   allocate the stream. */
sonicStream sonicCreateStream(
    double speed,
    int sampleRate)
{
    sonicStream stream = (sonicStream)calloc(1, sizeof(struct sonicStreamStruct));
    int minPeriod = sampleRate/SONIC_MAX_PITCH;
    int maxPeriod = sampleRate/SONIC_MIN_PITCH;
    int maxRequired = 2*maxPeriod; 

    if(stream == NULL) {
	return NULL;
    }
    stream->inputBuffer = (float *)calloc(maxRequired, sizeof(float));
    if(stream->inputBuffer == NULL) {
	sonicDestroyStream(stream);
	return NULL;
    }
    stream->outputBuffer = (float *)calloc(maxRequired, sizeof(float));
    if(stream->outputBuffer == NULL) {
	sonicDestroyStream(stream);
	return NULL;
    }
    stream->speed = speed;
    stream->sampleRate = sampleRate;
    stream->minPeriod = minPeriod;
    stream->maxPeriod = maxPeriod;
    stream->maxRequired = maxRequired;
    stream->inputBufferSize = maxRequired;
    stream->outputBufferSize = maxRequired;
    return stream;
}

/* Enlarge the output buffer if needed. */
static int enlargeOutputBufferIfNeeded(
    sonicStream stream,
    int numSamples)
{
    if(stream->numOutputSamples + numSamples > stream->outputBufferSize) {
	stream->outputBufferSize += (stream->outputBufferSize >> 1) + numSamples;
	stream->outputBuffer = (float *)realloc(stream->outputBuffer,
	    stream->outputBufferSize*sizeof(float));
	if(stream->outputBuffer == NULL) {
	    return 0;
	}
    }
    return 1;
}

/* Enlarge the input buffer if needed. */
static int enlargeInputBufferIfNeeded(
    sonicStream stream,
    int numSamples)
{
    if(stream->numInputSamples + numSamples > stream->inputBufferSize) {
	stream->inputBufferSize += (stream->inputBufferSize >> 1) + numSamples;
	stream->inputBuffer = (float *)realloc(stream->inputBuffer,
	    stream->inputBufferSize*sizeof(float));
	if(stream->inputBuffer == NULL) {
	    return 0;
	}
    }
    return 1;
}

/* Add the input samples to the input buffer. */
static int addSamplesToInputBuffer(
    sonicStream stream,
    float *samples,
    int numSamples)
{
    if(numSamples == 0) {
	return 1;
    }
    if(!enlargeInputBufferIfNeeded(stream, numSamples)) {
	return 0;
    }
    memcpy(stream->inputBuffer + stream->numInputSamples, samples, numSamples*sizeof(float));
    stream->numInputSamples += numSamples;
    return 1;
}

/* Remove input samples that we have already processed. */
static void removeInputSamples(
    sonicStream stream,
    int position)
{
    int remainingSamples = stream->numInputSamples - position;

    if(remainingSamples > 0) {
	memmove(stream->inputBuffer, stream->inputBuffer + position,
	    remainingSamples*sizeof(float));
    }
    stream->numInputSamples = remainingSamples;
}

/* Just copy from the array to the output buffer */
static int copyToOutput(
    sonicStream stream,
    float *samples,
    int numSamples)
{
    if(!enlargeOutputBufferIfNeeded(stream, numSamples)) {
	return 0;
    }
    memcpy(stream->outputBuffer + stream->numOutputSamples, samples, numSamples*sizeof(float));
    stream->numOutputSamples += numSamples;
    return numSamples;
}

/* Just copy from the input buffer to the output buffer.  Return 0 if we fail to
   resize the output buffer.  Otherwise, return numSamples */
static int copyInputToOutput(
    sonicStream stream,
    int position)
{
    int numSamples = stream->remainingInputToCopy;

    if(numSamples > stream->maxRequired) {
	numSamples = stream->maxRequired;
    }
    if(!copyToOutput(stream, stream->inputBuffer + position, numSamples)) {
	return 0;
    }
    stream->remainingInputToCopy -= numSamples;
    return numSamples;
}

/* Read data out of the stream.  Sometimes no data will be available, and zero
   is returned, which is not an error condition. */
int sonicReadFromStream(
    sonicStream stream,
    float *samples,
    int maxSamples)
{
    int numSamples = stream->numOutputSamples;
    int remainingSamples = 0;

    if(numSamples == 0) {
	return 0;
    }
    if(numSamples > maxSamples) {
	remainingSamples = numSamples - maxSamples;
	numSamples = maxSamples;
    }
    memcpy(samples, stream->outputBuffer, numSamples*sizeof(float));
    if(remainingSamples > 0) {
	memmove(stream->outputBuffer, stream->outputBuffer + numSamples,
	    remainingSamples*sizeof(float));
    }
    stream->numOutputSamples = remainingSamples;
    return numSamples;
}

/* Force the sonic stream to generate output using whatever data it currently
   has.  Zeros will be appended to the input data if there is not enough data
   in the stream's input buffer.  Use this, followed by a final read from the
   stream before destroying the stream. */
int sonicFlushStream(
    sonicStream stream)
{
    int maxRequired = stream->maxRequired;
    int numSamples = stream->numInputSamples;
    int remainingSpace;

    if(numSamples == 0) {
	return 1;
    }
    if(numSamples >= maxRequired && !sonicWriteToStream(stream, NULL, 0)) {
	return 0;
    }
    numSamples = stream->numInputSamples; /* Now numSamples < maxRequired */
    remainingSpace = maxRequired - numSamples;
    memset(stream->inputBuffer + numSamples, 0, remainingSpace*sizeof(float));
    stream->numInputSamples = maxRequired;
    return sonicWriteToStream(stream, NULL, 0);
}

/* Return the number of samples in the output buffer */
int sonicSamplesAvailale(
   sonicStream stream)
{
    return stream->numOutputSamples;
}

/* Find the best frequency match in the range, and given a sample skip multiple. */
static int findPitchPeriodInRange(
    sonicStream stream,
    float *samples,
    int minPeriod,
    int maxPeriod,
    int skip)
{
    int period, bestPeriod = 0;
    double minDiff = 0.0;
    double diff;
    float value, *s, *p;
    int xSample;

    for(period = minPeriod; period <= maxPeriod; period += skip) {
	diff = 0.0;
	s = samples;
	p = samples + period;
	for(xSample = 0; xSample < period; xSample += skip) {
	    value = *s - *p;
	    s += skip;
	    p += skip;
	    diff += value >= 0.0? value : -value;
	}
	if(bestPeriod == 0 || diff < minDiff*period) {
	    diff /= period;
	    minDiff = diff;
	    bestPeriod = period;
	}
    }
    return bestPeriod;
}

/* Find the pitch period.  This is a critical step, and we may have to try
   multiple ways to get a good answer.  This version uses AMDF.  To improve
   speed, we down sample by an integer factor get in the 11KHz range, and then
   do it again with a narrower frequency range without down sampling */
static int findPitchPeriod(
    sonicStream stream,
    float *samples)
{
    int minPeriod = stream->minPeriod;
    int maxPeriod = stream->maxPeriod;
    int sampleRate = stream->sampleRate;
    int skip = 1;
    int period;

    if(sampleRate > SONIC_AMDF_FREQ) {
	skip = sampleRate/SONIC_AMDF_FREQ;
    }
    period = findPitchPeriodInRange(stream, samples, minPeriod, maxPeriod, skip);
    minPeriod = period*(1.0 - SONIC_AMDF_RANGE);
    maxPeriod = period*(1.0 + SONIC_AMDF_RANGE);
    if(minPeriod < stream->minPeriod) {
	minPeriod = stream->minPeriod;
    }
    if(maxPeriod > stream->maxPeriod) {
	maxPeriod = stream->maxPeriod;
    }
    return findPitchPeriodInRange(stream, samples, minPeriod, maxPeriod, 1);
}

/* Skip over a pitch period, and copy period/speed samples to the output */
static int skipPitchPeriod(
    sonicStream stream,
    float *samples,
    double speed,
    int period)
{
    int t, newSamples;
    double scale;
    float *out;

    if(speed >= 2.0) {
	newSamples = period/(speed - 1.0);
    } else if(speed > 1.0) {
	newSamples = period;
	stream->remainingInputToCopy = period*(2.0 - speed)/(speed - 1.0);
    }
    scale = 1.0/newSamples;
    if(!enlargeOutputBufferIfNeeded(stream, newSamples)) {
	return 0;
    }
    out = stream->outputBuffer + stream->numOutputSamples;
    for(t = 0; t < newSamples; t++) {
        out[t] = scale*(samples[t]*(newSamples - t) + samples[t + period]*t);
    }
    stream->numOutputSamples += newSamples;
    return newSamples;
}

/* Insert a pitch period, and determine how much input to copy directly. */
static int insertPitchPeriod(
    sonicStream stream,
    float *samples,
    double speed,
    int period)
{
    int t, newSamples;
    double scale;
    float *out;

    if(speed < 0.5) {
        newSamples = period*speed/(1.0 - speed);
    } else {
        newSamples = period;
	stream->remainingInputToCopy = period*(2.0*speed - 1.0)/(1.0 - speed);
    }
    if(!enlargeOutputBufferIfNeeded(stream, period + newSamples)) {
	return 0;
    }
    out = stream->outputBuffer + stream->numOutputSamples;
    memcpy(out, samples, period*sizeof(float));
    out += period;
    scale = 1.0/newSamples;
    for(t = 0; t < newSamples; t++) {
        out[t] = scale*(samples[t]*t + samples[t + period]*(newSamples - t));
    }
    stream->numOutputSamples += period + newSamples;
    return newSamples;
}

/* Resample as many pitch periods as we have buffered on the input.  Return 0 if
   we fail to resize an input or output buffer */
int sonicWriteToStream(
    sonicStream stream,
    float *samples,
    int numSamples)
{
    double speed = stream->speed;
    int position = 0, period, newSamples;
    int maxRequired = stream->maxRequired;

    if(speed > 0.999999 && speed < 1.000001) {
	/* No speed change - just copy to the output */
	return copyToOutput(stream, samples, numSamples);
    }
    if(!addSamplesToInputBuffer(stream, samples, numSamples)) {
	return 0;
    }
    if(stream->numInputSamples < maxRequired) {
	return 1;
    }
    samples = stream->inputBuffer;
    numSamples = stream->numInputSamples;
    do {
	if(stream->remainingInputToCopy > 0) {
            newSamples = copyInputToOutput(stream, position);
	    position += newSamples;
	} else {
	    period = findPitchPeriod(stream, samples + position);
	    if(speed > 1.0) {
		newSamples = skipPitchPeriod(stream, samples + position, speed, period);
		position += period + newSamples;
	    } else {
		newSamples = insertPitchPeriod(stream, samples + position, speed, period);
		position += newSamples;
	    }
	}
	if(newSamples == 0) {
	    return 0; /* Failed to resize output buffer */
	}
    } while(position + maxRequired <= numSamples);
    removeInputSamples(stream, position);
    return 1;
}
