/*
 * Copyright 2009-2011 Red Hat, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _COLLECTABLE_CODEC_H
#define _COLLECTABLE_CODEC_H

// wso2 codegen types
#include <AviaryCommon_Attributes.h>
#include <AviaryCommon_Collector.h>
#include <AviaryCommon_Master.h>
#include <AviaryCommon_Negotiator.h>
#include <AviaryCommon_Scheduler.h>
#include <AviaryCommon_Submitter.h>
#include <AviaryCommon_Slot.h>
#include <AviaryCommon_Submitter.h>

// internal rep
#include "Collectables.h"

namespace aviary {
namespace collector {
    
class CollectableCodec{

public:
    CollectableCodec(axutil_env_t* env) { m_env = env; }
    AviaryCommon::Collector* encode(aviary::collector::Collector& in_);
    AviaryCommon::Master* encode(aviary::collector::Master& in_);
    AviaryCommon::Negotiator* encode(aviary::collector::Negotiator& in_);
    AviaryCommon::Scheduler* encode(aviary::collector::Scheduler& in_);
    AviaryCommon::Slot* encode(aviary::collector::Slot& in_);
    AviaryCommon::Submitter* encode(aviary::collector::Submitter& in_);

private:
    // axis2c env ptr for utils
    axutil_env_t* m_env;
    
};

}}

#endif /* _COLLECTABLE_CODEC_H */
