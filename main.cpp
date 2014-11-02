#include <alljoyn/BusAttachment.h>
#include "XMPPConnector.h"
#include <iostream>
#include <csignal>

using namespace ajn;
using namespace ajn::gw;

using std::cout;
using std::endl;

static BusAttachment* s_Bus = 0;
static XMPPConnector* s_Conn = 0;

void cleanup()
{
    if (s_Conn) {
        s_Conn->stop();
        delete s_Conn;
        s_Conn = 0;
    }

    if(s_Bus) {
        s_Bus->Disconnect();
        s_Bus->Stop();
        delete s_Bus;
        s_Bus = 0;
    }
}

static void SigIntHandler(int sig)
{
    if(s_Conn)
    {
        s_Conn->stop();
    }
    cleanup();
    exit(0);
}

int main(int argc, char** argv)
{
    signal(SIGINT, SigIntHandler);
    s_Bus = new BusAttachment("XMPPConnector", true);

    // Set up bus attachment
    QStatus status = s_Bus->Start();
    if (ER_OK != status) {
        cout << "Error starting bus: " << QCC_StatusText(status) << endl;
        cleanup();
        return 1;
    }

    status = s_Bus->Connect();
    if (ER_OK != status) {
        cout << "Error connecting bus: " << QCC_StatusText(status) << endl;
        cleanup();
        return 1;
    }

    // Create our XMPP connector
    s_Conn = new XMPPConnector(s_Bus, "XMPP", "pub@aus1.affinegy.com", "pub", "alljoyn@muc.aus1.affinegy.com");
    s_Conn->start();

    cleanup();
}

