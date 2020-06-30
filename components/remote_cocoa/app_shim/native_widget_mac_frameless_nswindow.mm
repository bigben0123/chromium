// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/remote_cocoa/app_shim/native_widget_mac_frameless_nswindow.h"

#ifndef MAS_BUILD

@interface NSWindow (PrivateAPI)
+ (Class)frameViewClassForStyleMask:(NSUInteger)windowStyle;
@end

@interface NativeWidgetMacFramelessNSWindowFrame
    : NativeWidgetMacNSWindowTitledFrame
@end

@implementation NativeWidgetMacFramelessNSWindowFrame
- (CGFloat)_titlebarHeight {
  return 0;
}
@end

#endif  // MAS_BUILD

@implementation NativeWidgetMacFramelessNSWindow

#ifndef MAS_BUILD

+ (Class)frameViewClassForStyleMask:(NSUInteger)windowStyle {
  if ([NativeWidgetMacFramelessNSWindowFrame class]) {
    return [NativeWidgetMacFramelessNSWindowFrame class];
  }
  return [super frameViewClassForStyleMask:windowStyle];
}

#endif  // MAS_BUILD

@end
