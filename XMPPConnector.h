#ifndef XMPPCONNECTOR_H_                                                        // TODO: document
#define XMPPCONNECTOR_H_

#ifndef NO_AJ_GATEWAY
#include <alljoyn/gateway/GatewayConnector.h>
#endif // !NO_AJ_GATEWAY
#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusListener.h>
#include <alljoyn/notification/Notification.h>
#include <alljoyn/notification/NotificationService.h>
#include <alljoyn/notification/NotificationSender.h>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <pthread.h>

#include <alljoyn/about/AboutPropertyStoreImpl.h>

namespace ajn {
namespace gw {

class AllJoynListener;
class RemoteBusAttachment;
class XmppTransport;

#ifdef NO_AJ_GATEWAY
class XMPPConnector
#else
class XMPPConnector : public GatewayConnector
#endif // NO_AJ_GATEWAY
{
    friend class XmppTransport;

public:
    XMPPConnector(
        BusAttachment*     bus,
        const std::string& appName,
        const std::string& xmppJid,
        const std::string& xmppPassword,
        const std::string& xmppChatroom
        );

    virtual ~XMPPConnector();

    // Blocks until stop() is called, listens for XMPP
    QStatus Start();
    void Stop();

    bool
    IsAdvertisingName(
        const std::string& name
        );


    // Class to handle custom XMPP messages
    class MessageReceiver
    {
    public:
        typedef
        void
        (XMPPConnector::MessageReceiver::* MessageHandler)(
            const std::string& key,
            const std::string& message,
            void*              userdata
            );
    };

    void
    RegisterMessageHandler(
        const std::string&                             key,
        XMPPConnector::MessageReceiver*                receiver,
        XMPPConnector::MessageReceiver::MessageHandler messageHandler,
        void*                                          userdata
        );

    void
    UnregisterMessageHandler(
        const std::string& key
        );

    void
    SendMessage(
        const std::string& key,
        const std::string& message
        );

protected:
#ifndef NO_AJ_GATEWAY
    virtual void mergedAclUpdated();
    virtual void shutdown();
    virtual void receiveGetMergedAclAsync(
        QStatus unmarshalStatus, GatewayMergedAcl* response);
#endif // !NO_AJ_GATEWAY

private:
    struct RemoteObjectDescription
    {
        std::string                        path;
        std::map<std::string, std::string> interfaces;
    };

    RemoteBusAttachment* GetRemoteAttachment(
        const std::string&                          remoteName,
        const std::vector<RemoteObjectDescription>* objects = NULL
        );
    void DeleteRemoteAttachment(
        RemoteBusAttachment*& attachment
        );

    BusAttachment*   m_bus;
    AllJoynListener* m_listener;

    std::list<RemoteBusAttachment*> m_remoteAttachments;
    pthread_mutex_t                 m_remoteAttachmentsMutex;

    struct MessageCallback
    {
        XMPPConnector::MessageReceiver*                receiver;
        XMPPConnector::MessageReceiver::MessageHandler messageHandler;
        void*                                          userdata;
    };
    std::map<std::string, MessageCallback> m_messageCallbackMap;

    XmppTransport* m_xmppTransport;
};

} //namespace gw
} //namespace ajn



#endif // XMPPCONNECTOR_H_

