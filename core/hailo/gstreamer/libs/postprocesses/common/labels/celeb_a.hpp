/**
* Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
* Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
**/

#pragma once
#include <map>
#include <stdint.h>
#include <string>

namespace labels 
{
    static std::map<uint8_t, std::string> celeb_a = {
        {0, "5_o_Clock_Shadow"},
        {1, "Arched_Eyebrows"},
        {2, "Attractive"},
        {3, "Bags_Under_Eyes"},
        {4, "Bald"},
        {5, "Bangs"},
        {6, "Big_Lips"},
        {7, "Big_Nose"},
        {8, "Black_Hair"},
        {9, "Blond_Hair"},
        {10, "Blurry"},
        {11, "Brown_Hair"},
        {12, "Bushy_Eyebrows"},
        {13, "Chubby"},
        {14, "Double_Chin"},
        {15, "Eyeglasses"},
        {16, "Goatee"},
        {17, "Gray_Hair"},
        {18, "Heavy_Makeup"},
        {19, "High_Cheekbones"},
        {20, "Male"},
        {21, "Mouth_Slightly_Open"},
        {22, "Mustache"},
        {23, "Narrow_Eyes"},
        {24, "No_Beard"},
        {25, "Oval_Face"},
        {26, "Pale_Skin"},
        {27, "Pointy_Nose"},
        {28, "Receding_Hairline"},
        {29, "Rosy_Cheeks"},
        {30, "Sideburns"},
        {31, "Smiling"},
        {32, "Straight_Hair"},
        {33, "Wavy_Hair"},
        {34, "Wearing_Earrings"},
        {35, "Wearing_Hat"},
        {36, "Wearing_Lipstick"},
        {37, "Wearing_Necklace"},
        {38, "Wearing_Necktie"},
        {39, "Young"}};

    static std::map<uint8_t, std::string> celeb_a_filtered = {
        {0, ""},
        {1, ""},
        {2, ""},
        {3, ""},
        {4, "Bald"},
        {5, "Bangs"},
        {6, ""},
        {7, ""},
        {8, "Black_Hair"},
        {9, "Blond_Hair"},
        {10, "Blurry"},
        {11, "Brown_Hair"},
        {12, ""},
        {13, ""},
        {14, ""},
        {15, "Eyeglasses"},
        {16, "Goatee"},
        {17, "Gray_Hair"},
        {18, ""},
        {19, "High_Cheekbones"},
        {20, "Male"},
        {21, "Mouth_Slightly_Open"},
        {22, "Mustache"},
        {23, ""},
        {24, "No_Beard"},
        {25, ""},
        {26, ""},
        {27, ""},
        {28, ""},
        {29, ""},
        {30, "Sideburns"},
        {31, "Smiling"},
        {32, "Straight_Hair"},
        {33, "Wavy_Hair"},
        {34, "Wearing_Earrings"},
        {35, "Wearing_Hat"},
        {36, "Wearing_Lipstick"},
        {37, "Wearing_Necklace"},
        {38, "Wearing_Necktie"},
        {39, ""}};
}