#ifndef CSPYDR_VERSION_H
#define CSPYDR_VERION_H

#define CSPYDR_VERSION_X "0"
#define CSPYDR_VERSION_Y "0"
#define CSPYDR_VERSION_Z "5"

#ifdef DEBUG
    #define CSPYDR_VERSION_W "d"
#else
    #define CSPYDR_VERSION_W "r"
#endif

void get_cspydr_build(char* dest);
const char* get_cspydr_version();

#endif