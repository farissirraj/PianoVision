# PianoVision

## Image
We made use of the Debian IoT (non-graphical image) found [here](https://www.beagleboard.org/distros/beaglebone-black-debian-12-12-2025-10-29-iot-v5-10-t).

## PianoVision Client Initial Setup
1. Manually assign a local IP Address to your system's ethernet interface. We used `192.168.137.1`. 
2. ssh into the BeagleBone using `ssh debian@BeagleBone`. Change the password when prompted.
3. On your host system, share the internet (WiFi in our case) to the ethernet port so that the BeagleBone also gets access to the internet. This will enable you to install the necessary packages.

## Installation of Packages on the BeagleBone Black
Perform the following steps on the BeagleBone Black.

Install `gstreamer`
```
sudo apt-get install gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
```

Test if gstreamer can fetch the frames from the camera device and display it to the LCD screen:
```
gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,width=640,height=480 ! videoconvert ! videoscale ! video/x-raw,width=480,height=272 ! fbdevsink device=/dev/fb0
```

Install `OpenCV`
```
sudo apt-get install libopencv-dev python3-opencv
```

## Compilation of PianoVision Client
For this project, we performed compilation on the BeagleBone itself without needing to cross-compile it on the host system.
```
g++ main.cpp network.h cv.h config.h config.cpp -o exec_final `pkg-config --cflags --libs opencv4`
```

## Environment Setup of PianoVision Server
PianoVision server script runs a web application using StreamLit. The required dependencies are as follows:
```
streamlit==1.51.0
numpy==2.2.6
PyAudio==0.2.14
opencv-contrib-python==4.12.0.88
opencv-python==4.12.0.88
```

## Run the PianoVision Server
```
streamlit run web_vis.py
```