#!/bin/sh

PROJECT_NAME=$(find . -name "*.xcodeproj" | sed 's/\.xcodeproj//g' | sed 's/^\.\///g' | tr '[:upper:]' '[:lower:]')
VERSION=$(grep "#define VERSION " src/include/config.h | cut -d " " -f 3 | tr -d '"')
echo "Building $PROJECT_NAME.xpl version $VERSION. Is this correct? (y/n):"
read CONFIRM

if [ -z "$CONFIRM" ]; then
    CONFIRM="y"
fi

if [ "$CONFIRM" != "y" ]; then
    echo "Please update the version number in config.h and try again."
    exit 1
fi

AVAILABLE_PLATFORMS="mac win lin"
echo "Which platforms would you like to build? ($AVAILABLE_PLATFORMS):"
read PLATFORMS

if [ -z "$PLATFORMS" ]; then
    PLATFORMS=$AVAILABLE_PLATFORMS
fi

for platform in $PLATFORMS; do
    if ! echo $AVAILABLE_PLATFORMS | grep -q $platform; then
        echo "Invalid platform: $platform. Exiting."
        exit 1
    fi
done

echo "Building for platforms: \033[1m$PLATFORMS\033[0m\n"

echo "Which SDK version do you want to build with? (301/410):"
read SDK_VERSION

if [ -z "$SDK_VERSION" ]; then
    SDK_VERSION=410
fi

echo "Building with SDK version $SDK_VERSION\n"
echo "Clean build directory? (y/n):"
read CLEAN_BUILD

if [ -z "$CLEAN_BUILD" ]; then
    CLEAN_BUILD="n"
fi

echo "Create additional files for the plugin? (y/n):"
read EXTRA_FILES

if [ -z "$EXTRA_FILES" ]; then
    EXTRA_FILES="n"
fi

if [ "$CLEAN_BUILD" = "y" ]; then
    echo "Cleaning build directories..."
    if [ -d "build" ]; then
        rm -rf build
    fi
fi

for platform in $PLATFORMS; do
    echo "Building $platform..."
    if [ $platform = "lin" ]; then
        # Prerequisite: Create a container based on gcc:latest with cmake and libgl1-mesa-dev, for x86_64 architecture
        docker run --rm -v $(pwd):/src -w /src gcc-cmake-x86:latest bash -c "\
        cmake -DCMAKE_CXX_FLAGS="-march=x86-64" -DCMAKE_TOOLCHAIN_FILE=toolchain-$platform.cmake -DSDK_VERSION=$SDK_VERSION -Bbuild/$platform -H. && \
        make -C build/$platform"
    else
        cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-$platform.cmake -DSDK_VERSION=$SDK_VERSION -Bbuild/$platform -H.
        make -C build/$platform
    fi

    if [ $? -eq 0 ]; then
        echo "\n\n"
        echo "\033[1;32m$platform build succeeded.\033[0m\nProduct: build/$platform/${platform}_x64/${PROJECT_NAME}.xpl"
        file build/$platform/${platform}_x64/${PROJECT_NAME}.xpl
        sleep 3
    else
        echo "\033[1;31m$platform build failed.\033[0m"
        exit 1
    fi
done

echo "Building has finished."

echo "Creating distribution bundle..."
if [ -d "build/dist" ]; then
    rm -rf build/dist
fi

for platform in $AVAILABLE_PLATFORMS; do
    mkdir -p build/dist/${platform}_x64
    if [ -d "build/$platform/${platform}_x64" ]; then
        cp build/$platform/${platform}_x64/${PROJECT_NAME}.xpl build/dist/${platform}_x64/${PROJECT_NAME}.xpl
    fi

    if echo $PLATFORMS | grep -q $platform && [ -d "lib/${platform}_x64/dist_${SDK_VERSION}" ]; then
        cp -r lib/${platform}_x64/dist_${SDK_VERSION}/* build/dist/${platform}_x64
    fi

    if echo $PLATFORMS | grep -q $platform && [ -d "lib/${platform}_x64/dist_extra_${SDK_VERSION}" ] && [ "$EXTRA_FILES" = "y" ]; then
        mkdir -p build/extra_${platform}/${platform}_x64
        cp -r lib/${platform}_x64/dist_extra_${SDK_VERSION}/* build/extra_${platform}/${platform}_x64
    fi
done

cp -r assets build/dist
# Don't copy the default .ini file, will be created by the plugin on first run
# default_ini=$(sed -n '/const char \*defaultConfig = R"(/,/^)";/p' "./src/include/appstate.cpp" | sed '$d' | sed '1s/const char \*defaultConfig = R"(//' | sed 's/^[ \t]*//')
# echo "$default_ini" > build/dist/config.ini

# Only add Skunkcrafts for XP12
if [ $SDK_VERSION -ge 400 ]; then
    echo "module|https://ramonster.nl/avitab-browser\nname|AviTab Browser\nversion|$VERSION\nlocked|false\ndisabled|false\nzone|custom" > build/dist/skunkcrafts_updater.cfg
fi

cd build
mv dist $PROJECT_NAME

if [ $SDK_VERSION -lt 400 ]; then
    XPLANE_VERSION=XP11
else
    XPLANE_VERSION=XP12
fi

VERSION=$VERSION-$XPLANE_VERSION

rm -f $PROJECT_NAME-$VERSION.zip
zip -rq $PROJECT_NAME-$VERSION.zip $PROJECT_NAME -x "*/.DS_Store" -x "*/__MACOSX/*"

if [ "$EXTRA_FILES" = "y" ]; then
    for platform in $PLATFORMS; do
        if [ -d "extra_${platform}" ]; then
            echo "The '${platform}_x64' folder contains additional files required by the $PROJECT_NAME plugin for $XPLANE_VERSION.\nUnzip and merge these files with the plugin folder in your X-Plane installation directory, usually located at 'Resources/plugins/$PROJECT_NAME/${platform}_x64'." > extra_${platform}/README.txt
            rm -f $XPLANE_VERSION-$platform-additional-files.zip
            zip -rq $XPLANE_VERSION-$platform-additional-files.zip extra_${platform} -x ".DS_Store" -x "__MACOSX"
        fi
    done
fi

mv $PROJECT_NAME dist
cd ..

echo "Bundle created. Distribution: build/dist/$PROJECT_NAME-$VERSION.zip"
