#include "image.h"
#include "config.h"
#include "appstate.h"
#include <fstream>
#include <XPLMGraphics.h>
#include <XPLMUtilities.h>
#include "lodepng.h"
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

Image::Image(std::string filename) {
    x = 0;
    y = 0;
    rotationDegrees = 0;
    visible = true;
    textureId = 0;
    
    if (filename.empty()) {
        return;
    }
    
    unsigned char *data = nullptr;
    unsigned int error = 1;
    std::ifstream fileExistsHandle(filename);
    if (fileExistsHandle.good()) {
        if (filename.ends_with(".png") || filename.ends_with(".PNG")) {
            error = lodepng_decode32_file(&data, &pixelsWidth, &pixelsHeight, filename.c_str());
        }
        else if (filename.ends_with(".svg") || filename.ends_with(".svg")) {
            NSVGimage *image = nsvgParseFromFile(filename.c_str(), "px", 96);
            struct NSVGrasterizer *rast = nsvgCreateRasterizer();
            pixelsWidth = 16;
            pixelsHeight = 16;
            data = (unsigned char *)malloc(pixelsWidth * pixelsHeight * 4);
            nsvgRasterize(rast, image, 0, 0, (float)pixelsWidth / image->width, data, pixelsWidth, pixelsHeight, pixelsWidth * 4);
            for (int i = 0; i < pixelsWidth * pixelsHeight * 4; i++) {
                if (data[i] != 0) {
                    error = 0;
                    break;
                }
            }
        }
    }
    
    if (error) {
        debug("Could not load image (code %i): %s\n", error, filename.c_str());
        return;
    }

    if (data) {
        XPLMGenerateTextureNumbers(&textureId, 1);
        XPLMBindTexture2d(textureId, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pixelsWidth, pixelsHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        free(data);
    }
    
#if SCALE_IMAGES
    // Images have been designed for 800px width resolution. Scale to size.
    float aspectRatio = (float)pixelsWidth / pixelsHeight;
    pixelsWidth = (pixelsWidth * (float)AppState::getInstance()->tabletDimensions.width) / 800.0f;
    pixelsHeight = pixelsWidth / aspectRatio;
#endif
    
    relativeWidth = pixelsWidth / (float)AppState::getInstance()->tabletDimensions.width;
    relativeHeight = pixelsHeight / (float)AppState::getInstance()->tabletDimensions.height;
}

void Image::destroy() {
    if (textureId) {
        XPLMBindTexture2d(textureId, 0);
        glDeleteTextures(1, (GLuint *)&textureId);
        textureId = 0;
    }
}


void Image::draw(unsigned short aRotationDegrees) {
    rotationDegrees = aRotationDegrees;
    draw();
}

void Image::draw() {
    if (!textureId || !visible) {
        return;
    }
    
    XPLMSetGraphicsState(
                         0, // No fog, equivalent to glDisable(GL_FOG);
                         1, // One texture, equivalent to glEnable(GL_TEXTURE_2D);
                         0, // No lighting, equivalent to glDisable(GL_LIGHT0);
                         0, // No alpha testing, e.g glDisable(GL_ALPHA_TEST);
                         1, // Use alpha blending, e.g. glEnable(GL_BLEND);
                         0, // No depth read, e.g. glDisable(GL_DEPTH_TEST);
                         0 // No depth write, e.g. glDepthMask(GL_FALSE);
    );
    
    XPLMBindTexture2d(textureId, 0);
    
    unsigned short x1 = AppState::getInstance()->tabletDimensions.x + x;
    unsigned short y1 = AppState::getInstance()->tabletDimensions.y + y;
    
    if (rotationDegrees > 0) {
        glPushMatrix();
        //glLoadIdentity();
        glTranslatef(x1, y1, 0.0f);
        glRotatef(rotationDegrees, 0.0f, 0.0f, -1.0f);
        glTranslatef(-x1, -y1, 0.0f);
    }
    
    x1 -= pixelsWidth / 2.0f;
    y1 -= pixelsHeight / 2.0f;
    
    glBegin(GL_QUADS);
    set_brightness(AppState::getInstance()->brightness);
    
    glTexCoord2f(0, 1);
    glVertex2f(x1, y1);
    
    glTexCoord2f(0, 0);
    glVertex2f(x1, y1 + pixelsHeight);
    
    glTexCoord2f(1, 0);
    glVertex2f(x1 + pixelsWidth, y1 + pixelsHeight);
    
    glTexCoord2f(1, 1);
    glVertex2f(x1 + pixelsWidth, y1);
    
    glEnd();
    
    if (rotationDegrees > 0) {
        glPopMatrix();
    }
}

void Image::setPosition(float normalizedX, float normalizedY, unsigned short aRotationDegrees) {
    x = AppState::getInstance()->tabletDimensions.width * normalizedX;
    y = AppState::getInstance()->tabletDimensions.height * normalizedY;
    rotationDegrees = aRotationDegrees;
}
