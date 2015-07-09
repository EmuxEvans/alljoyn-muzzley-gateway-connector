#ifndef CONFIG_DATA_STORE_H_
#define CONFIG_DATA_STORE_H_

#include "app/ConfigParser.h"
#include <stdio.h>
#include <iostream>
#include <alljoyn/config/AboutDataStoreInterface.h>
#include <alljoyn/BusAttachment.h>

class ConfigDataStore : public AboutDataStoreInterface {
  public:
    typedef void (*RestartCallback)();

    ConfigDataStore(const char* factoryConfigFile, const char* configFile, RestartCallback func);
    void FactoryReset();
    const qcc::String& GetConfigFileName();
    virtual ~ConfigDataStore();
    virtual QStatus ReadAll(const char* languageTag, DataPermission::Filter filter, ajn::MsgArg& all);
    virtual QStatus Update(const char* name, const char* languageTag, const ajn::MsgArg* value);
    virtual QStatus Delete(const char* name, const char* languageTag);
    void Initialize(qcc::String deviceId = qcc::String(), qcc::String appId = qcc::String());
  private:
    bool m_IsInitialized;
    qcc::String m_configFileName;
    qcc::String m_factoryConfigFileName;
    QStatus IsLanguageSupported(const char* languageTag);
    RestartCallback m_restartCallback;
    ConfigParser* m_configParser;
};

#endif
