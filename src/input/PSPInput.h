#pragma once
#include <pspctrl.h>

// Input PSP via sceCtrl

void PSPInput_Update();

// Analog sticks: idx=0 stang, idx=1 drept (simulat cu butoane pe PSP-ul fara
// stick drept)
float PSPInput_StickX(int idx);
float PSPInput_StickY(int idx);

// Butoane
bool PSPInput_IsHeld(unsigned int button);
bool PSPInput_JustPressed(unsigned int button);
bool PSPInput_JustReleased(unsigned int button);
