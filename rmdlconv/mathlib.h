// Copyright (c) 2022, rexx
// See LICENSE.txt for licensing information (GPLv3)

#pragma once

struct Quaternion
{
	float x, y, z, w;
};

struct RadianEuler
{
	float x, y, z;
};

struct QAngle
{
	float x, y, z;
};

struct matrix3x4_t
{
	float m[3][4];
};