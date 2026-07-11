#ifndef IMAGE_H
#define IMAGE_H

#include <string>
#include <vector>

class Image {
private:
    int textureId;
    // Decoded pixels waiting for texture creation, which can only happen
    // inside a draw callback under the XP12 Metal renderer. The texture must
    // be created with the decode-time dimensions; pixelsWidth/pixelsHeight are
    // rescaled to display size by SCALE_IMAGES right after decoding.
    std::vector<unsigned char> pendingPixels;
    unsigned int decodedWidth;
    unsigned int decodedHeight;
    void createTexture();

protected:
    short x;
    short y;
    unsigned short rotationDegrees;
    
public:
    bool visible;
    float relativeWidth;
    float relativeHeight;
    unsigned int pixelsWidth;
    unsigned int pixelsHeight;
    
    Image(std::string filename);
    void destroy();
    void draw(unsigned short rotationDegrees);
    virtual void draw();
    virtual void setPosition(float normalizedX, float normalizedY, unsigned short rotationDegrees = 0);
};

#endif
