/** 
 * Copyright (c) 2015, Affinegy, Inc.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any 
 * purpose with or without fee is hereby granted, provided that the above 
 * copyright notice and this permission notice appear in all copies. 
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES 
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY 
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES 
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION 
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 */ 

#ifndef CONFIGSERVICELISTENERIMPL_H_
#define CONFIGSERVICELISTENERIMPL_H_
#pragma once

#include "ConfigDataStore.h"
#include "app/MUZZLEYConnector.h"
#include "common/CommonBusListener.h"

#include <alljoyn/config/ConfigService.h>
#include <string>

class SrpKeyXListener;

class ConfigServiceListenerImpl : public ajn::services::ConfigService::Listener {
    public:
        ConfigServiceListenerImpl(
                ConfigDataStore& store,
                ajn::BusAttachment& bus,
                CommonBusListener* busListener, 
                void(*func)(), 
                const std::string& configFilePath
                );

        virtual ~ConfigServiceListenerImpl();
        virtual QStatus Restart();
        virtual QStatus FactoryReset();
        virtual QStatus SetPassphrase(
                const char* daemonRealm,
                size_t passcodeSize,
                const char* passcode,
                ajn::SessionId sessionId
                );
    private:
        ConfigDataStore* m_ConfigDataStore;
        CommonBusListener* m_BusListener;
        SrpKeyXListener* m_KeyListener;
        ajn::BusAttachment* m_Bus;
        std::string m_ConfigFilePath;

        void (*m_onRestartCallback)();
        void PersistPassword(const char* daemonRealm, const char* passcode);
};

#endif
