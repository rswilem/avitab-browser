#ifndef IMAGE_H
#define IMAGE_H

#include <string>

class Image {
private:
    int textureId;
    
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
