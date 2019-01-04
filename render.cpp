#include <Bela.h>
#include <cmath>
#include <SampleData.h>
#include <sndfile.h>
#include <cstdlib>

#define NUM_CHANNELS 2    // NUMBER OF CHANNELS IN THE FILE
#define BUFFER_LEN 2048   // BUFFER LENGTH

float gDuration = 10 * 44100;
float sorted[BUFFER_LEN*2];
int gCount = 0;

// figure out how to pass vars to AuxiliaryTask / use less globals
// name of file generated
SampleData gSampleBuf[2][NUM_CHANNELS];
//index value of buffer array
int gPos = 0;

//start on second buffer bc it will switch on first run thru
int gActiveBuffer = 0;
int gDoneLoadingBuffer = 1;
int gChunk = 0;

AuxiliaryTask gFillBufferTask;

SF_INFO sfinfo;

const char* path = "./test.wav";

SNDFILE * outfile;

void fileSetup() {
    sfinfo.channels = NUM_CHANNELS;
    sfinfo.samplerate = 44100;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
}

void writeFile(float *buf, int startSamp, int endSamp){
    //calculate duration of write operation - wouldn't this just be BUFFER_LEN?
    int frameLen = endSamp - startSamp;

    ///sf_seek(outfile, startSamp, SEEK_SET);

    //to use sf_write_xxxx functions, you must declare it as an sf_count variable
    //because it returns the number of samples/frames written (but this value does not
    //necessarily need to be used)
    //                 sf_write_float(pointer to file opened by sf_open, array start, array length)
    sf_count_t count = sf_write_float(outfile, &buf[0], frameLen);

}

void fillBuffer(void*) {

    int end = gChunk + (BUFFER_LEN*2);

    for (size_t i = 0; i < BUFFER_LEN; i++) {
        sorted[i*2] = gSampleBuf[!gActiveBuffer][0].samples[i];
        sorted[(i*2)+1] = gSampleBuf[!gActiveBuffer][1].samples[i];
    }

    writeFile(sorted, gChunk, end);

    printf("writing file at %d - %d\n", gChunk, end);

    //signal the file is written
    gDoneLoadingBuffer = 1;
    //increment by BUFFER_LEN
    gChunk += BUFFER_LEN;
}

bool setup(BelaContext *context, void *userData) {

    //initialize auxiliary task
    if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0) {
        return false;
    }

    //initialize sample data struct with buffers/arrays
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++) {
            gSampleBuf[i][ch].sampleLen = BUFFER_LEN;
            gSampleBuf[i][ch].samples = new float[BUFFER_LEN];
        }
    }

    fileSetup(); //because sfinfo definitions must be in a function?

    //open sndfile pointer??
    // sf_open(filepath, mode, pointer to sfinfo)
    //     mode can be read, write, or read/write
    outfile = sf_open(path, SFM_WRITE, &sfinfo);

    return true;
}

void render(BelaContext *context, void *userData) {
    for(unsigned int n = 0; n < context->audioFrames; n++) {

        float inL = audioRead(context, n, 0);
        float inR = audioRead(context, n, 1);

        //swap buffers when gPos reaches BUFFER_LEN
        if(gPos == 0) {
            if(!gDoneLoadingBuffer && gCount <= gDuration && gCount > 0) {
                printf("increase buffer size!\n");
            }

            gDoneLoadingBuffer = 0;
            gActiveBuffer = !gActiveBuffer;
            if (gCount < gDuration) {
                Bela_scheduleAuxiliaryTask(gFillBufferTask);
            }
        }

        //if samples counted are more than duration, mute, otherwise output sine and write file
        if (gCount > gDuration) {
            for(unsigned int ch = 0; ch < context->audioOutChannels; ch++) {
                audioWrite(context, n, ch, 0);
            }
        } else {

            gSampleBuf[gActiveBuffer][0].samples[gPos] = inL;
            audioWrite(context, n, 0, inL);
            gSampleBuf[gActiveBuffer][1].samples[gPos] = inR;
            audioWrite(context, n, 1, inR);
        }

        gCount++;
        gPos = gCount%BUFFER_LEN;
    }
}

void cleanup(BelaContext *context, void *userData) {
    //i think this writes the appropriate header info for the file?
    //     ie non-audio data for wav, aiff, etc
    sf_write_sync(outfile);
    //closes file
    sf_close(outfile);

    // Delete the allocated buffers
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++) {
            delete[] gSampleBuf[i][ch].samples;
        }
    }
}
