#import <XCTest/XCTest.h>
#import <UIKit/UIKit.h>

static BOOL hasPlayerColor(UIImage *image)
{
    if (!image.CGImage) return NO;
    enum { Width = 64, Height = 64 };
    unsigned char pixels[Width * Height * 4] = {0};
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels, Width, Height, 8,
        Width * 4, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context) return NO;
    CGContextDrawImage(context, CGRectMake(0, 0, Width, Height), image.CGImage);
    CGContextRelease(context);
    NSUInteger cyanPixels = 0;
    for (NSUInteger offset = 0; offset < sizeof(pixels); offset += 4)
    {
        const int red = pixels[offset], green = pixels[offset + 1], blue = pixels[offset + 2];
        if (blue > 120 && green > red + 40 && blue > red + 60) ++cyanPixels;
    }
    return cyanPixels > 10;
}

@interface MobileUITests : XCTestCase
@end

@implementation MobileUITests

- (void)testAuthoredTouchAndSurfaceRecovery
{
    XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.cppgameengine.mobile"];
    [app launch];
    XCTAssertEqual(app.state, XCUIApplicationStateRunningForeground);

    // The authored Player mesh is visible in the center of the startup scene.
    // The right-half tap presses the manifest's move_right binding and Lua
    // moves the mesh, so the rendered screenshot must change.
    [NSThread sleepForTimeInterval:2.0];
    UIImage *beforeImage = [app screenshot].image;
    NSData *before = UIImagePNGRepresentation(beforeImage);
    XCTAssertGreaterThan(before.length, (NSUInteger)0);
    XCTAssertTrue(hasPlayerColor(beforeImage));
    [[app coordinateWithNormalizedOffset:CGVectorMake(0.75, 0.50)] tap];
    [NSThread sleepForTimeInterval:1.0];
    UIImage *afterTouchImage = [app screenshot].image;
    NSData *afterTouch = UIImagePNGRepresentation(afterTouchImage);
    XCTAssertTrue(hasPlayerColor(afterTouchImage));
    XCTAssertNotEqualObjects(before, afterTouch);

    [[XCUIDevice sharedDevice] pressButton:XCUIDeviceButtonHome];
    [app activate];
    XCTAssertEqual(app.state, XCUIApplicationStateRunningForeground);
    [NSThread sleepForTimeInterval:1.0];
    XCTAssertTrue(hasPlayerColor([app screenshot].image));

    [XCUIDevice sharedDevice].orientation = UIDeviceOrientationLandscapeLeft;
    [NSThread sleepForTimeInterval:1.0];
    XCTAssertEqual(app.state, XCUIApplicationStateRunningForeground);
    XCTAssertTrue(hasPlayerColor([app screenshot].image));
    [XCUIDevice sharedDevice].orientation = UIDeviceOrientationPortrait;
}

@end
