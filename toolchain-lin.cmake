set(CMAKE_SYSTEM_NAME Linux)
# The build image points gcc/g++ at 13 via update-alternatives (see
# docker/Dockerfile.linux). Naming a version here pins us to a compiler the
# image may not ship, which is how the g++-12 build broke.
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
