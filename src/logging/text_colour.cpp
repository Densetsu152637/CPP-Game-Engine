//
// Created by Nicholas on 21/04/26.
//

// initialisation of static constants

#include "text_colour.h"

#include <format>

const std::string TextColour::RESET = "\u001b[0m";

const TextColour

// shades
TextColour::WHITE         = TextColour { Colour::WHITE, false },
TextColour::GREY          = TextColour { Colour::GREY , false },
TextColour::BLACK         = TextColour { Colour::BLACK, false },

// red
TextColour::PINK          = TextColour { Colour::PINK   , false },
TextColour::ROSE          = TextColour { Colour::ROSE   , false },
TextColour::RED           = TextColour { Colour::RED    , false },
TextColour::CRIMSON       = TextColour { Colour::CRIMSON, false },

// blue
TextColour::CYAN          = TextColour { Colour::CYAN      , false },
TextColour::SKY_BLUE      = TextColour { Colour::SKY_BLUE  , false },
TextColour::BLUE          = TextColour { Colour::BLUE      , false },
TextColour::ROYAL_BLUE    = TextColour { Colour::ROYAL_BLUE, false },
TextColour::DARK_BLUE     = TextColour { Colour::DARK_BLUE , false },

// green
TextColour::LIME          = TextColour { Colour::LIME        , false },
TextColour::GREEN         = TextColour { Colour::GREEN       , false },
TextColour::FOREST_GREEN  = TextColour { Colour::FOREST_GREEN, false },
TextColour::DARK_GREEN    = TextColour { Colour::DARK_GREEN  , false },

// yellow
TextColour::LIGHT_YELLOW  = TextColour { Colour::LIGHT_YELLOW, false },
TextColour::YELLOW        = TextColour { Colour::YELLOW      , false },
TextColour::MUSTARD       = TextColour { Colour::MUSTARD     , false },
TextColour::DARK_YELLOW   = TextColour { Colour::DARK_YELLOW , false },

// orange
TextColour::ORANGE        = TextColour { Colour::ORANGE    , false },
TextColour::BROWN         = TextColour { Colour::BROWN     , false },
TextColour::DARK_BROWN    = TextColour { Colour::DARK_BROWN, false },

// purple
TextColour::LAVENDER      = TextColour { Colour::LAVENDER     , false },
TextColour::MAGENTA       = TextColour { Colour::MAGENTA      , false },
TextColour::PURPLE        = TextColour { Colour::PURPLE       , false },
TextColour::DARK_LAVENDER = TextColour { Colour::DARK_LAVENDER, false },
TextColour::GRAPE         = TextColour { Colour::GRAPE        , false },

// shades
TextColour::WHITE_BACKGROUND           = TextColour { Colour::WHITE, true },
TextColour::GREY_BACKGROUND            = TextColour { Colour::GREY , true },
TextColour::BLACK_BACKGROUND           = TextColour { Colour::BLACK, true },

// red
TextColour::PINK_BACKGROUND            = TextColour { Colour::PINK   , true },
TextColour::ROSE_BACKGROUND            = TextColour { Colour::ROSE   , true },
TextColour::RED_BACKGROUND             = TextColour { Colour::RED    , true },
TextColour::CRIMSON_BACKGROUND         = TextColour { Colour::CRIMSON, true },

// blue
TextColour::CYAN_BACKGROUND            = TextColour { Colour::CYAN      , true },
TextColour::SKY_BLUE_BACKGROUND        = TextColour { Colour::SKY_BLUE  , true },
TextColour::BLUE_BACKGROUND            = TextColour { Colour::BLUE      , true },
TextColour::ROYAL_BLUE_BACKGROUND      = TextColour { Colour::ROYAL_BLUE, true },
TextColour::DARK_BLUE_BACKGROUND       = TextColour { Colour::DARK_BLUE , true },

// green
TextColour::LIME_BACKGROUND            = TextColour { Colour::LIME        , true },
TextColour::GREEN_BACKGROUND           = TextColour { Colour::GREEN       , true },
TextColour::FOREST_GREEN_BACKGROUND    = TextColour { Colour::FOREST_GREEN, true },
TextColour::DARK_GREEN_BACKGROUND      = TextColour { Colour::DARK_GREEN  , true },

// yellow
TextColour::LIGHT_YELLOW_BACKGROUND    = TextColour { Colour::LIGHT_YELLOW, true },
TextColour::YELLOW_BACKGROUND          = TextColour { Colour::YELLOW      , true },
TextColour::MUSTARD_BACKGROUND         = TextColour { Colour::MUSTARD     , true },
TextColour::DARK_YELLOW_BACKGROUND     = TextColour { Colour::DARK_YELLOW , true },

// orange
TextColour::ORANGE_BACKGROUND          = TextColour { Colour::ORANGE    , true },
TextColour::BROWN_BACKGROUND           = TextColour { Colour::BROWN     , true },
TextColour::DARK_BROWN_BACKGROUND      = TextColour { Colour::DARK_BROWN, true },

// purple
TextColour::LAVENDER_BACKGROUND        = TextColour { Colour::LAVENDER     , true },
TextColour::MAGENTA_BACKGROUND         = TextColour { Colour::MAGENTA      , true },
TextColour::PURPLE_BACKGROUND          = TextColour { Colour::PURPLE       , true },
TextColour::DARK_LAVENDER_BACKGROUND   = TextColour { Colour::DARK_LAVENDER, true },
TextColour::GRAPE_BACKGROUND           = TextColour { Colour::GRAPE        , true };

std::string TextColour::from_txt_clr() const
{
    return std::format("\u001b[{};2;{};{};{}m",
            b ? "48" : "38",
            c.r,
            c.g,
            c.b
        );
}
