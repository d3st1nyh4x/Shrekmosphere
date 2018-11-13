/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <switch.h>

#include <atmosphere/version.h>

#include "fatal_task_screen.hpp"
#include "fatal_config.hpp"
#include "fatal_font.hpp"
#include "ams_logo.hpp"

static constexpr u32 FatalScreenWidth = 1280;
static constexpr u32 FatalScreenHeight = 720;
static constexpr u32 FatalScreenBpp = 2;

static constexpr u32 FatalScreenWidthAlignedBytes = (FatalScreenWidth * FatalScreenBpp + 63) & ~63;
static constexpr u32 FatalScreenWidthAligned = FatalScreenWidthAlignedBytes / FatalScreenBpp;

u32 GetPixelOffset(uint32_t x, uint32_t y)
{
    u32 tmp_pos;

    tmp_pos = ((y & 127) / 16) + (x/32*8) + ((y/16/8)*(((FatalScreenWidthAligned/2)/16*8)));
    tmp_pos *= 16*16 * 4;

    tmp_pos += ((y%16)/8)*512 + ((x%32)/16)*256 + ((y%8)/2)*64 + ((x%16)/8)*32 + (y%2)*16 + (x%8)*2;//This line is a modified version of code from the Tegra X1 datasheet.

    return tmp_pos / 2;
}

Result ShowFatalTask::SetupDisplayInternal() {
    Result rc;
    ViDisplay display;
    /* Try to open the display. */
    if (R_FAILED((rc = viOpenDisplay("Internal", &display)))) {
        if (rc == 0xE72) {
            return 0;
        } else {
            return rc;
        }
    }
    /* Guarantee we close the display. */
    ON_SCOPE_EXIT { viCloseDisplay(&display); };
    
    /* Turn on the screen. */
    if (R_FAILED((rc = viSetDisplayPowerState(&display, ViPowerState_On)))) {
        return rc;
    }
    
    /* Set alpha to 1.0f. */
    if (R_FAILED((rc = viSetDisplayAlpha(&display, 1.0f)))) {
        return rc;
    }
    
    return rc;
}

Result ShowFatalTask::SetupDisplayExternal() {
    Result rc;
    ViDisplay display;
    /* Try to open the display. */
    if (R_FAILED((rc = viOpenDisplay("External", &display)))) {
        if (rc == 0xE72) {
            return 0;
        } else {
            return rc;
        }
    }
    /* Guarantee we close the display. */
    ON_SCOPE_EXIT { viCloseDisplay(&display); };
    
    /* Set alpha to 1.0f. */
    if (R_FAILED((rc = viSetDisplayAlpha(&display, 1.0f)))) {
        return rc;
    }
    
    return rc;
}

Result ShowFatalTask::PrepareScreenForDrawing() {
    Result rc = 0;
    
    /* Connect to vi. */
    if (R_FAILED((rc = viInitialize(ViServiceType_Manager)))) {
        return rc;
    }
    
    /* Close other content. */
    viSetContentVisibility(false);
    
    /* Setup the two displays. */
    if (R_FAILED((rc = SetupDisplayInternal())) || R_FAILED((rc = SetupDisplayExternal()))) {
        return rc;
    }
    
    /* Open the default display. */
    if (R_FAILED((rc = viOpenDefaultDisplay(&this->display)))) {
        return rc;
    }
    
    /* Reset the display magnification to its default value. */
    u32 display_width, display_height;
    if (R_FAILED((rc = viGetDisplayLogicalResolution(&this->display, &display_width, &display_height)))) {
        return rc;
    }
    if (R_FAILED((rc = viSetDisplayMagnification(&this->display, 0, 0, display_width, display_height)))) {
        return rc;
    }
    
    /* Create layer to draw to. */
    if (R_FAILED((rc = viCreateLayer(&this->display, &this->layer)))) {
        return rc;
    }
    
    /* Setup the layer. */
    {
        /* Display a layer of 1280 x 720 at 1.5x magnification */
        /* NOTE: N uses 2 (770x400) RGBA4444 buffers (tiled buffer + linear). */
        /* We use a single 1280x720 tiled RGB565 buffer. */
        constexpr u32 raw_width = FatalScreenWidth;
        constexpr u32 raw_height = FatalScreenHeight;
        constexpr u32 layer_width = ((raw_width) * 3) / 2;
        constexpr u32 layer_height = ((raw_height) * 3) / 2;
        
        const float layer_x = static_cast<float>((display_width - layer_width) / 2);
        const float layer_y = static_cast<float>((display_height - layer_height) / 2);
        u64 layer_z;
        
        if (R_FAILED((rc = viSetLayerSize(&this->layer, layer_width, layer_height)))) {
            return rc;
        }
        
        /* Set the layer's Z at display maximum, to be above everything else .*/
        /* NOTE: Fatal hardcodes 100 here. */
        if (R_SUCCEEDED((rc = viGetDisplayMaximumZ(&this->display, &layer_z)))) {
            if (R_FAILED((rc = viSetLayerZ(&this->layer, layer_z)))) {
                return rc;
            }
        }
        
        /* Center the layer in the screen. */
        if (R_FAILED((rc = viSetLayerPosition(&this->layer, layer_x, layer_y)))) {
            return rc;
        }
    
        /* Create framebuffer. */
        if (R_FAILED(rc = nwindowCreateFromLayer(&this->win, &this->layer))) {
            return rc;
        }
        if (R_FAILED(rc = framebufferCreate(&this->fb, &this->win, raw_width, raw_height, PIXEL_FORMAT_RGB_565, 1))) {
            return rc;
        }
    }
    

    return rc;
}

Result ShowFatalTask::ShowFatal() {
    Result rc = 0;

    if (R_FAILED((rc = PrepareScreenForDrawing()))) {
        *(volatile u32 *)(0xCAFEBABE) = rc;
        return rc;
    }
    
    /* Dequeue a buffer. */
    u16 *tiled_buf = reinterpret_cast<u16 *>(framebufferBegin(&this->fb, NULL));
    if (tiled_buf == nullptr) {
        return FatalResult_NullGfxBuffer;
    }
    
    /* Let the font manager know about our framebuffer. */
    FontManager::ConfigureFontFramebuffer(tiled_buf, GetPixelOffset);
    FontManager::SetFontColor(0xFFFF);
    
    /* Draw a background. */
    for (size_t i = 0; i < this->fb.fb_size / sizeof(*tiled_buf); i++) {
        tiled_buf[i] = 0x39C9;
    }
    
    /* Draw the atmosphere logo in the bottom right corner. */
    for (size_t y = 0; y < AMS_LOGO_HEIGHT; y++) {
        for (size_t x = 0; x < AMS_LOGO_WIDTH; x++) {
            tiled_buf[GetPixelOffset(FatalScreenWidth - AMS_LOGO_WIDTH - 32 + x, FatalScreenHeight - AMS_LOGO_HEIGHT - 32 + y)] = AMS_LOGO_BIN[y * AMS_LOGO_WIDTH + x];
        }
    }
    
    /* TODO: Actually draw meaningful shit here. */
    FontManager::SetPosition(32, 64);
    FontManager::PrintFormatLine(u8"A fatal error occurred: 2%03d-%04d", R_MODULE(this->ctx->error_code), R_DESCRIPTION(this->ctx->error_code));
    FontManager::AddSpacingLines(0.5f);
    FontManager::PrintFormatLine(u8"Firmware: %s (Atmosphère %u.%u.%u-%s)", GetFatalConfig()->firmware_version.display_version, 
                                CURRENT_ATMOSPHERE_VERSION, GetAtmosphereGitRevision());
    
    
    /* Enqueue the buffer. */
    framebufferEnd(&fb);
    
    return rc;
}

Result ShowFatalTask::Run() {
    /* Don't show the fatal error screen until we've verified the battery is okay. */
    eventWait(this->battery_event, U64_MAX);

    return ShowFatal();
}

void BacklightControlTask::TurnOnBacklight() {
    lblSwitchBacklightOn(0);
}

Result BacklightControlTask::Run() {
    TurnOnBacklight();
    return 0;
}
