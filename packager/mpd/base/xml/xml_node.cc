// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/xml/xml_node.h"

#include <limits>
#include <set>

#include "packager/base/logging.h"
#include "packager/base/macros.h"
#include "packager/base/stl_util.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/segment_info.h"

using edash_packager::xml::XmlNode;

using edash_packager::MediaInfo;
typedef edash_packager::MediaInfo::AudioInfo AudioInfo;
typedef edash_packager::MediaInfo::VideoInfo VideoInfo;
typedef MediaInfo::ContentProtectionXml ContentProtectionXml;
typedef ContentProtectionXml::AttributeNameValuePair AttributeNameValuePair;

namespace edash_packager {

namespace {

std::string RangeToString(const Range& range) {
  return base::Uint64ToString(range.begin()) + "-" +
         base::Uint64ToString(range.end());
}

bool PopulateSegmentTimeline(const std::list<SegmentInfo>& segment_infos,
                             XmlNode* segment_timeline) {
  for (std::list<SegmentInfo>::const_iterator it = segment_infos.begin();
       it != segment_infos.end();
       ++it) {
    XmlNode s_element("S");
    s_element.SetIntegerAttribute("t", it->start_time);
    s_element.SetIntegerAttribute("d", it->duration);
    if (it->repeat > 0)
      s_element.SetIntegerAttribute("r", it->repeat);

    CHECK(segment_timeline->AddChild(s_element.PassScopedPtr()));
  }

  return true;
}

}  // namespace

namespace xml {

XmlNode::XmlNode(const char* name) : node_(xmlNewNode(NULL, BAD_CAST name)) {
  DCHECK(name);
  DCHECK(node_);
}

XmlNode::~XmlNode() {}

bool XmlNode::AddChild(ScopedXmlPtr<xmlNode>::type child) {
  DCHECK(node_);
  DCHECK(child);
  if (!xmlAddChild(node_.get(), child.get()))
    return false;

  // Reaching here means the ownership of |child| transfered to |node_|.
  // Release the pointer so that it doesn't get destructed in this scope.
  ignore_result(child.release());
  return true;
}

bool XmlNode::AddElements(const std::vector<Element>& elements) {
  for (size_t element_index = 0; element_index < elements.size();
       ++element_index) {
    const Element& child_element = elements[element_index];
    XmlNode child_node(child_element.name.c_str());
    for (std::map<std::string, std::string>::const_iterator attribute_it =
             child_element.attributes.begin();
         attribute_it != child_element.attributes.end(); ++attribute_it) {
      child_node.SetStringAttribute(attribute_it->first.c_str(),
                                    attribute_it->second);
    }
    // Recursively set children for the child.
    if (!child_node.AddElements(child_element.subelements))
      return false;

    child_node.SetContent(child_element.content);

    if (!xmlAddChild(node_.get(), child_node.GetRawPtr())) {
      LOG(ERROR) << "Failed to set child " << child_element.name
                 << " to parent element "
                 << reinterpret_cast<const char*>(node_->name);
      return false;
    }
    // Reaching here means the ownership of |child_node| transfered to |node_|.
    // Release the pointer so that it doesn't get destructed in this scope.
    ignore_result(child_node.Release());
  }
  return true;
}

void XmlNode::SetStringAttribute(const char* attribute_name,
                                 const std::string& attribute) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlSetProp(node_.get(), BAD_CAST attribute_name, BAD_CAST attribute.c_str());
}

void XmlNode::SetIntegerAttribute(const char* attribute_name, uint64_t number) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlSetProp(node_.get(),
             BAD_CAST attribute_name,
             BAD_CAST (base::Uint64ToString(number).c_str()));
}

void XmlNode::SetFloatingPointAttribute(const char* attribute_name,
                                        double number) {
  DCHECK(node_);
  DCHECK(attribute_name);
  xmlSetProp(node_.get(),
             BAD_CAST attribute_name,
             BAD_CAST (base::DoubleToString(number).c_str()));
}

void XmlNode::SetId(uint32_t id) {
  SetIntegerAttribute("id", id);
}

void XmlNode::SetContent(const std::string& content) {
  DCHECK(node_);
  xmlNodeSetContent(node_.get(), BAD_CAST content.c_str());
}

ScopedXmlPtr<xmlNode>::type XmlNode::PassScopedPtr() {
  DVLOG(2) << "Passing node_.";
  DCHECK(node_);
  return node_.Pass();
}

xmlNodePtr XmlNode::Release() {
  DVLOG(2) << "Releasing node_.";
  DCHECK(node_);
  return node_.release();
}

xmlNodePtr XmlNode::GetRawPtr() {
  return node_.get();
}

RepresentationBaseXmlNode::RepresentationBaseXmlNode(const char* name)
    : XmlNode(name) {}
RepresentationBaseXmlNode::~RepresentationBaseXmlNode() {}

bool RepresentationBaseXmlNode::AddContentProtectionElements(
    const std::list<ContentProtectionElement>& content_protection_elements) {
  std::list<ContentProtectionElement>::const_iterator content_protection_it =
      content_protection_elements.begin();
  for (; content_protection_it != content_protection_elements.end();
       ++content_protection_it) {
    if (!AddContentProtectionElement(*content_protection_it))
      return false;
  }

  return true;
}

bool RepresentationBaseXmlNode::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  XmlNode content_protection_node("ContentProtection");

  // @value is an optional attribute.
  if (!content_protection_element.value.empty()) {
    content_protection_node.SetStringAttribute(
        "value", content_protection_element.value);
  }
  content_protection_node.SetStringAttribute(
      "schemeIdUri", content_protection_element.scheme_id_uri);

  typedef std::map<std::string, std::string> AttributesMapType;
  const AttributesMapType& additional_attributes =
      content_protection_element.additional_attributes;

  AttributesMapType::const_iterator attributes_it =
      additional_attributes.begin();
  for (; attributes_it != additional_attributes.end(); ++attributes_it) {
    content_protection_node.SetStringAttribute(attributes_it->first.c_str(),
                                               attributes_it->second);
  }

  if (!content_protection_node.AddElements(
          content_protection_element.subelements)) {
    return false;
  }
  return AddChild(content_protection_node.PassScopedPtr());
}

AdaptationSetXmlNode::AdaptationSetXmlNode()
    : RepresentationBaseXmlNode("AdaptationSet") {}
AdaptationSetXmlNode::~AdaptationSetXmlNode() {}

void AdaptationSetXmlNode::AddRoleElement(const std::string& scheme_id_uri,
                                          const std::string& value) {
  XmlNode role("Role");
  role.SetStringAttribute("schemeIdUri", scheme_id_uri);
  role.SetStringAttribute("value", value);
  AddChild(role.PassScopedPtr());
}

RepresentationXmlNode::RepresentationXmlNode()
    : RepresentationBaseXmlNode("Representation") {}
RepresentationXmlNode::~RepresentationXmlNode() {}

bool RepresentationXmlNode::AddVideoInfo(const VideoInfo& video_info) {
  if (!video_info.has_width() || !video_info.has_height()) {
    LOG(ERROR) << "Missing width or height for adding a video info.";
    return false;
  }

  if (video_info.has_pixel_width() && video_info.has_pixel_height()) {
    SetStringAttribute("sar", base::IntToString(video_info.pixel_width()) +
                                  ":" +
                                  base::IntToString(video_info.pixel_height()));
  }

  SetIntegerAttribute("width", video_info.width());
  SetIntegerAttribute("height", video_info.height());
  SetStringAttribute("frameRate",
                     base::IntToString(video_info.time_scale()) + "/" +
                         base::IntToString(video_info.frame_duration()));
  return true;
}

bool RepresentationXmlNode::AddAudioInfo(const AudioInfo& audio_info) {
  if (!AddAudioChannelInfo(audio_info))
    return false;

  AddAudioSamplingRateInfo(audio_info);
  return true;
}

bool RepresentationXmlNode::AddVODOnlyInfo(const MediaInfo& media_info) {
  if (media_info.has_media_file_name()) {
    XmlNode base_url("BaseURL");
    base_url.SetContent(media_info.media_file_name());

    if (!AddChild(base_url.PassScopedPtr()))
      return false;
  }

  const bool need_segment_base = media_info.has_index_range() ||
                                 media_info.has_init_range() ||
                                 media_info.has_reference_time_scale();

  if (need_segment_base) {
    XmlNode segment_base("SegmentBase");
    if (media_info.has_index_range()) {
      segment_base.SetStringAttribute("indexRange",
                                      RangeToString(media_info.index_range()));
    }

    if (media_info.has_reference_time_scale()) {
      segment_base.SetIntegerAttribute("timescale",
                                       media_info.reference_time_scale());
    }

    if (media_info.has_init_range()) {
      XmlNode initialization("Initialization");
      initialization.SetStringAttribute("range",
                                        RangeToString(media_info.init_range()));

      if (!segment_base.AddChild(initialization.PassScopedPtr()))
        return false;
    }

    if (!AddChild(segment_base.PassScopedPtr()))
      return false;
  }

  if (media_info.has_media_duration_seconds()) {
    // Adding 'duration' attribute, so that this information can be used when
    // generating one MPD file. This should be removed from the final MPD.
    SetFloatingPointAttribute("duration", media_info.media_duration_seconds());
  }

  return true;
}

bool RepresentationXmlNode::AddLiveOnlyInfo(
    const MediaInfo& media_info,
    const std::list<SegmentInfo>& segment_infos,
    uint32_t start_number) {
  XmlNode segment_template("SegmentTemplate");
  if (media_info.has_reference_time_scale()) {
    segment_template.SetIntegerAttribute("timescale",
                                         media_info.reference_time_scale());
  }

  if (media_info.has_init_segment_name()) {
    // The spec does not allow '$Number$' and '$Time$' in initialization
    // attribute.
    // TODO(rkuroiwa, kqyang): Swap this check out with a better check. These
    // templates allow formatting as well.
    const std::string& init_segment_name = media_info.init_segment_name();
    if (init_segment_name.find("$Number$") != std::string::npos ||
        init_segment_name.find("$Time$") != std::string::npos) {
      LOG(ERROR) << "$Number$ and $Time$ cannot be used for "
                    "SegmentTemplate@initialization";
      return false;
    }
    segment_template.SetStringAttribute("initialization",
                                        media_info.init_segment_name());
  }

  if (media_info.has_segment_template()) {
    segment_template.SetStringAttribute("media", media_info.segment_template());

    // TODO(rkuroiwa): Need a better check. $$Number is legitimate but not a
    // template.
    if (media_info.segment_template().find("$Number") != std::string::npos) {
      DCHECK_GE(start_number, 1u);
      segment_template.SetIntegerAttribute("startNumber", start_number);
    }
  }

  // TODO(rkuroiwa): Find out when a live MPD doesn't require SegmentTimeline.
  XmlNode segment_timeline("SegmentTimeline");

  return PopulateSegmentTimeline(segment_infos, &segment_timeline) &&
         segment_template.AddChild(segment_timeline.PassScopedPtr()) &&
         AddChild(segment_template.PassScopedPtr());
}

bool RepresentationXmlNode::AddAudioChannelInfo(const AudioInfo& audio_info) {
  const uint32_t num_channels = audio_info.num_channels();
  XmlNode audio_channel_config("AudioChannelConfiguration");
  const char kAudioChannelConfigScheme[] =
      "urn:mpeg:dash:23003:3:audio_channel_configuration:2011";
  audio_channel_config.SetStringAttribute("schemeIdUri",
                                          kAudioChannelConfigScheme);
  audio_channel_config.SetIntegerAttribute("value", num_channels);

  return AddChild(audio_channel_config.PassScopedPtr());
}

// MPD expects one number for sampling frequency, or if it is a range it should
// be space separated.
void RepresentationXmlNode::AddAudioSamplingRateInfo(
    const AudioInfo& audio_info) {
  if (audio_info.has_sampling_frequency())
    SetIntegerAttribute("audioSamplingRate", audio_info.sampling_frequency());
}

}  // namespace xml
}  // namespace edash_packager
