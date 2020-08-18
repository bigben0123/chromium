// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/picture_in_picture/html_video_element_picture_in_picture.h"

#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/modules/picture_in_picture/html_element_picture_in_picture.h"
#include "third_party/blink/renderer/modules/picture_in_picture/picture_in_picture_controller_impl.h"
#include "third_party/blink/renderer/modules/picture_in_picture/picture_in_picture_window.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#ifndef CUST_NO_EVENT_NOTIFY_METHOD  // zhibin:method notify headers
#include "third_party/blink/renderer/core/events/mutation_event.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#endif

namespace blink {

// static
ScriptPromise HTMLVideoElementPictureInPicture::requestPictureInPicture(
    ScriptState* script_state,
    HTMLVideoElement& element,
    ExceptionState& exception_state) {
  HTMLElementPictureInPicture::CheckIfPictureInPictureIsAllowed(
      element, nullptr /* options */, exception_state);
  if (exception_state.HadException())
    return ScriptPromise();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  auto promise = resolver->Promise();

  PictureInPictureControllerImpl::From(element.GetDocument())
      .EnterPictureInPicture(&element, nullptr /* options */, resolver);

#ifndef CUST_NO_EVENT_NOTIFY_METHOD  // zhibin:method video element requestPictureInPicture()
  {
    LocalDOMWindow* executing_window = element.GetDocument().ExecutingWindow();
    MutationEvent* me = MutationEvent::Create(
        event_type_names::kDOMCharacterDataModified, Event::Bubbles::kNo,
        nullptr, "", "", "HTMLMediaElement.requestPictureInPicture", 0);
    me->SetType("cust_event_notify_method");
    executing_window->DispatchEvent(*me);
  }
#endif 
  return promise;
}

// static
bool HTMLVideoElementPictureInPicture::FastHasAttribute(
    const QualifiedName& name,
    const HTMLVideoElement& element) {
  DCHECK(name == html_names::kDisablepictureinpictureAttr ||
         name == html_names::kAutopictureinpictureAttr);
  return element.FastHasAttribute(name);
}

// static
void HTMLVideoElementPictureInPicture::SetBooleanAttribute(
    const QualifiedName& name,
    HTMLVideoElement& element,
    bool value) {
  DCHECK(name == html_names::kDisablepictureinpictureAttr ||
         name == html_names::kAutopictureinpictureAttr);
  element.SetBooleanAttribute(name, value);

  Document& document = element.GetDocument();
  TreeScope& scope = element.GetTreeScope();
  PictureInPictureControllerImpl& controller =
      PictureInPictureControllerImpl::From(document);

  if (name == html_names::kDisablepictureinpictureAttr && value &&
      controller.PictureInPictureElement(scope) == &element) {
    controller.ExitPictureInPicture(&element, nullptr);
  }
}

}  // namespace blink
