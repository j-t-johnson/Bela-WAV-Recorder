#include <Bela.h>
#include <cmath>
#include <SampleData.h>
#include <sndfile.h>
#include <cstdlib>

#define NUM_CHANNELS 2    // NUMBER OF CHANNELS IN THE FILE
#define BUFFER_LEN 2048   // BUFFER LENGTH

float gDuration = 20 * (60 * 44100);
//                ^ length of file in minutes

float sorted[BUFFER_LEN*2];
int gCount = 1;
int gArm = 0;
int gPreroll = 0;

// figure out how to pass vars to AuxiliaryTask / use less globals
// name of file generated
SampleData gSampleBuf[2][NUM_CHANNELS];
//index value of buffer array
int gPos = 1;

//start on second buffer bc it will switch on first run thru
int gActiveBuffer = 0;
int gDoneLoadingBuffer = 1;
int gChunk = 0;

////////////////////////////////
int bNow, bPrev;
int bEnable = 0;

int closed = 0;

AuxiliaryTask gFillBufferTask;
AuxiliaryTask gCloseFileTask;
AuxiliaryTask gOpenFileTask;

SF_INFO sfinfo;

//const char* path = "/root/media/sd/record.wav";
const char* path = "/root/other/wavs/record.wav";

SNDFILE * outfile;

void openFile(void*) {
    /* open sndfile pointer??
       sf_open(filepath, mode, pointer to sfinfo)
                         mode can be read, write, or read/write
     */
    outfile = sf_open(path, SFM_WRITE, &sfinfo);
    printf(".wav file open and writing\n");
}

void closeFile(void*) {
    sf_write_sync(outfile);
    sf_close(outfile);
    printf(".wav file written and closed\n");
}

void fileSetup() {
    sfinfo.channels = NUM_CHANNELS;
    sfinfo.samplerate = 44100;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
}

void writeFile(float *buf, int startSamp, int endSamp){
    //calculate duration of write operation - wouldn't this just be BUFFER_LEN?
    int frameLen = endSamp - startSamp;

    /*
    to use sf_write_xxxx functions, you must declare it as an sf_count variable
    because it returns the number of samples/frames written (but this value does not
    necessarily need to be used)
                     sf_write_float(pointer to file opened by sf_open, array start, array length)
    */
    sf_count_t count = sf_write_float(outfile, &buf[0], frameLen);

}

void fillBuffer(void*) {

    int end = gChunk + (BUFFER_LEN*2);

    for (size_t i = 0; i < BUFFER_LEN; i++) {
        sorted[i*2] = gSampleBuf[!gActiveBuffer][0].samples[i];
        sorted[(i*2)+1] = gSampleBuf[!gActiveBuffer][1].samples[i];
    }

    writeFile(sorted, gChunk, end);

    //printf("writing file at %d - %d\n", gChunk, end);

    //signal the file is written
    gDoneLoadingBuffer = 1;
    //increment by BUFFER_LEN
    gChunk += BUFFER_LEN;
}

bool setup(BelaContext *context, void *userData) {

    //LED and Button pins
    pinMode(context, 0, 1, INPUT);      //P8_08 // button
    pinMode(context, 0, 0, OUTPUT);     //P8_07 // active LED
    pinMode(context, 0, 10, OUTPUT);    //P9_16 // record LED

    //initialize auxiliary task
    if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0) {
        return false;
    }

    if((gCloseFileTask = Bela_createAuxiliaryTask(&closeFile, 80, "close-file")) == 0) {
        return false;
    }

    if((gOpenFileTask = Bela_createAuxiliaryTask(&openFile, 85, "open-file")) == 0) {
        return false;
    }

    fileSetup(); //because sfinfo definitions must be in a function?

    //initialize sample data struct with buffers/arrays
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++) {
            gSampleBuf[i][ch].sampleLen = BUFFER_LEN;
            gSampleBuf[i][ch].samples = new float[BUFFER_LEN];
        }
    }

    return true;

}

void render(BelaContext *context, void *userData) {

    for(unsigned int n = 0; n < context -> digitalFrames; n++){

        digitalWriteOnce(context, n, 10, 1); //write the status to the LED

        bNow = 1 - digitalRead(context, 0, 1); //read the inverse value of the button

        if (bNow != bPrev) {
            if (bNow == 1) {
                if (bEnable == 0) {
                    bEnable = 1;
                } else {
                    bEnable = 0;
                }
            }
        }

        bPrev = bNow;
        digitalWriteOnce(context, n, 0, bEnable); //write the status to the LED
    }

    for(unsigned int n = 0; n < context->audioFrames; n++) {

        float inL = audioRead(context, n, 0);
        float inR = audioRead(context, n, 1);

        //swap buffers when gPos reaches BUFFER_LEN
        if(gPos == 0) {
            if(!gDoneLoadingBuffer && gCount <= gDuration && gCount > 0) {
                printf("dropped\n");
            }

            gDoneLoadingBuffer = 0;
            gActiveBuffer = !gActiveBuffer;
            if (gCount < gDuration) {
                Bela_scheduleAuxiliaryTask(gFillBufferTask);
            }
        }

        //if samples counted are more than duration, mute, otherwise output sine and write file
        if (bEnable == 1) {
            if (gArm == 0) {
                gArm = 1;
                Bela_scheduleAuxiliaryTask(gOpenFileTask);
            } else if (gArm > 0) {
                gPreroll++;
            }

            if (gPreroll > BUFFER_LEN * 2) {
                gSampleBuf[gActiveBuffer][0].samples[gPos] = inL;
                audioWrite(context, n, 0, inL);
                gSampleBuf[gActiveBuffer][1].samples[gPos] = inR;
                audioWrite(context, n, 1, inR);

                gCount++;

                if (gCount >= gDuration){
                    bEnable = 0;
                    gCount = 1;
                    Bela_scheduleAuxiliaryTask(gCloseFileTask);
                    closed = 1;
                }
            }

        } else {
            for(unsigned int ch = 0; ch < context->audioOutChannels; ch++) {
                    audioWrite(context, n, ch, 0);
                }

        }
        gPos = gCount%BUFFER_LEN;
    }
}

void cleanup(BelaContext *context, void *userData) {
    if (closed == 0) {
        Bela_scheduleAuxiliaryTask(gCloseFileTask);
        closed = 1;
    }

    // Delete the allocated buffers
    for(int ch=0;ch<NUM_CHANNELS;ch++) {
        for(int i=0;i<2;i++) {
            delete[] gSampleBuf[i][ch].samples;
        }
    }
}
