/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once
#include <cstdint>

static const int32_t BT709_Y_R  =  13933;  // 0.2126 * 65536
static const int32_t BT709_Y_G  =  46871;  // 0.7152 * 65536
static const int32_t BT709_Y_B  =   4732;  // 0.0722 * 65536
static const int32_t BT709_CB_R = -7508;   // -0.1146 * 65536
static const int32_t BT709_CB_G = -25259;  // -0.3854 * 65536
static const int32_t BT709_CB_B =  32768;  // 0.5000 * 65536
static const int32_t BT709_CR_R =  32768;  // 0.5000 * 65536
static const int32_t BT709_CR_G = -29763;  // -0.4542 * 65536
static const int32_t BT709_CR_B = -3005;   // -0.0458 * 65536
static const int32_t BT2020_Y_R  =  17218;  // 0.2627 * 65536
static const int32_t BT2020_Y_G  =  44444;  // 0.6780 * 65536
static const int32_t BT2020_Y_B  =   3886;  // 0.0593 * 65536
static const int32_t BT2020_CB_R = -9147;   // -0.1396 * 65536
static const int32_t BT2020_CB_G = -23621;  // -0.3604 * 65536
static const int32_t BT2020_CB_B =  32768;  // 0.5000 * 65536
static const int32_t BT2020_CR_R =  32768;  // 0.5000 * 65536
static const int32_t BT2020_CR_G = -30134;  // -0.4598 * 65536
static const int32_t BT2020_CR_B = -2634;   // -0.0402 * 65536
