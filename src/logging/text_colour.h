//
// Created by Nicholas on 21/04/26.
//

#pragma once

#include <cstdint>
#include <string>

union ARGB
{

    uint32_t hex = 0X00000000;
    struct
    {
        uint8_t a, r, g, b;
    };

    ARGB(const int vh) : hex(vh) {}

};

namespace Colour
{
    static const ARGB
    WHITE          = 0XFFFFFF,
    GREY           = 0X777777,
    BLACK          = 0X000000,

    // blue
    CYAN           = 0X00FFFF,
    SKY_BLUE       = 0X0077FF,
    BLUE           = 0X0000FF,
    ROYAL_BLUE     = 0X002077,
    DARK_BLUE      = 0X000040,

    // red
    PINK           = 0XFFBFBF,
    ROSE           = 0XFF4040,
    RED            = 0XFF0000,
    CRIMSON        = 0X400000,

    // green
    LIME           = 0X77FF77,
    GREEN          = 0X00FF00,
    FOREST_GREEN   = 0X00A000,
    DARK_GREEN     = 0X004000,

    // yellow
    LIGHT_YELLOW   = 0XFFFFBF,
    YELLOW         = 0XFFFF00,
    MUSTARD        = 0XFFBF00,
    DARK_YELLOW    = 0X777700,

    // orange
    ORANGE         = 0XFF7700,
    BROWN          = 0X774000,
    DARK_BROWN     = 0X402000,

    // purple
    LAVENDER       = 0XFFDFFF,
    MAGENTA        = 0XFF00FF,
    PURPLE         = 0X7700FF,
    DARK_LAVENDER  = 0X7700BF,
    GRAPE          = 0X400077;

}


struct TextColour
{

    ARGB c;
    bool b;

    TextColour() : c(0x00000000), b(false) {}
    TextColour(ARGB colour, bool background) {
        c = colour;
        b = background;
    }

    // static

    const static std::string RESET;

    const static TextColour
    // shades
    WHITE,
    GREY,
    BLACK,

    // red
    PINK,
    ROSE,
    RED,
    CRIMSON,

    // blue
    CYAN,
    SKY_BLUE,
    BLUE,
    ROYAL_BLUE,
    DARK_BLUE,

    // green
    LIME,
    GREEN,
    FOREST_GREEN,
    DARK_GREEN,

    // yellow
    LIGHT_YELLOW,
    YELLOW,
    MUSTARD,
    DARK_YELLOW,

    // orange
    ORANGE,
    BROWN,
    DARK_BROWN,

    // purple
    LAVENDER,
    MAGENTA,
    PURPLE,
    DARK_LAVENDER,
    GRAPE,

    // shades
    WHITE_BACKGROUND,
    GREY_BACKGROUND,
    BLACK_BACKGROUND,

    // red
    PINK_BACKGROUND,
    ROSE_BACKGROUND,
    RED_BACKGROUND,
    CRIMSON_BACKGROUND,

    // blue
    CYAN_BACKGROUND,
    SKY_BLUE_BACKGROUND,
    BLUE_BACKGROUND,
    ROYAL_BLUE_BACKGROUND,
    DARK_BLUE_BACKGROUND,

    // green
    LIME_BACKGROUND,
    GREEN_BACKGROUND,
    FOREST_GREEN_BACKGROUND,
    DARK_GREEN_BACKGROUND,

    // yellow
    LIGHT_YELLOW_BACKGROUND,
    YELLOW_BACKGROUND,
    MUSTARD_BACKGROUND,
    DARK_YELLOW_BACKGROUND,

    // orange
    ORANGE_BACKGROUND,
    BROWN_BACKGROUND,
    DARK_BROWN_BACKGROUND,

    // purple
    LAVENDER_BACKGROUND,
    MAGENTA_BACKGROUND,
    PURPLE_BACKGROUND,
    DARK_LAVENDER_BACKGROUND,
    GRAPE_BACKGROUND;

    std::string from_txt_clr();
};





