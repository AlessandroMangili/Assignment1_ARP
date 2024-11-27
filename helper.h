#ifndef HELPER_H
#define HELPER_H

#include <stdio.h>
#include <stdlib.h>

// Costanti per le dimensioni della box del keyboard_manager
#define BOX_HEIGHT 3
#define BOX_WIDTH 5
#define INFO_WIDTH 40 // Larghezza della finestra di destra
#define PADDING 2     // Spaziatura tra le sezioni

typedef struct
{
    float position_x, position_y;
    float velocity_x, velocity_y;
    float force_x, force_y;
} Info;

//typedef Info info;

#endif
