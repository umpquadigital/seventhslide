#include "capture_devices_mac.hpp"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>

namespace seventhslide::app {

std::vector<MacCamera> mac_cameras()
{
    std::vector<MacCamera> out;
    @autoreleasepool {
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]
            == AVAuthorizationStatusNotDetermined) {
            // Listing needs no leave; opening does, and FFmpeg does not ask.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL granted) { (void)granted; }];
        }
        // The device types FFmpeg's avfoundation input lists, in its order
        // (libavdevice/avfoundation.m getDevicesWithMediaType, built for
        // macOS 14 or later), so a camera's place here is its place there.
        NSArray<AVCaptureDeviceType>* types = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera,
            AVCaptureDeviceTypeDeskViewCamera,
            AVCaptureDeviceTypeContinuityCamera,
            AVCaptureDeviceTypeExternal,
        ];
        AVCaptureDeviceDiscoverySession* session = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:types
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];
        int index = -1;
        for (AVCaptureDevice* device in session.devices) {
            ++index;
            MacCamera camera;
            camera.index = index;
            if (const char* name = device.localizedName.UTF8String) {
                camera.name = name;
            }
            if (const char* id = device.uniqueID.UTF8String) {
                camera.unique_id = id;
            }
            for (AVFrameRateRange* range in device.activeFormat.videoSupportedFrameRateRanges) {
                camera.frame_rate = std::max(camera.frame_rate, range.maxFrameRate);
            }
            if (!camera.name.empty()) {
                out.push_back(std::move(camera));
            }
        }
    }
    return out;
}

bool mac_camera_allowed()
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]
           == AVAuthorizationStatusAuthorized;
}

}  // namespace seventhslide::app
