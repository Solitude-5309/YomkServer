#pragma once
#include <yomk/msg/YomkMsg.h>
#include "ProjectPayload.h"

YOMK_DEFINE_MSG_NS(prj, yomk::payload::prj::ConfigKey, ConfigKey, req);
// 访问: ptr->req.key
YOMK_DEFINE_MSG_NS(prj, yomk::payload::prj::ConfigKeyValue, ConfigKeyValue, req);
// 访问: ptr->req.key, ptr->req.value