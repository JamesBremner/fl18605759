#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace std;

#define MAX_PACKET_SIZE_BYTES 1024

// set work time to 2 seconds
// to slow things down for debugfging purposes
// you can reduce this to 500 for production
#define WORK_TIME_MSECS 2000

/** A non-blocking TCP client */
class cNonBlockingTCPClient
{
public:

    /** CTOR
        param[in] io_service the event manager
    */

    cNonBlockingTCPClient(
        boost::asio::io_service& io_service )
        : myIOService( io_service )
        , myTimer( new boost::asio::deadline_timer( io_service ))
    {

    }

    /** Connect to server
        @param[in] ip address of server
        @param[in] port server is listening to for connections

        This does not return until the connection attempt successds or fails.
        The return occurs so quickly that it does not seem wiorthwhile
        to make this non-blocking.

        On successful connection a pre-defined message is sent to the server
        this is non-blocking and when the message has been sent handle_connect_write() will be called
    */
    void Connect(
        const std::string& ip,
        const std::string& port);

    /** read message from server
        @param[in] byte_count to be read

        This is non-blocking, returning immediatly.
        When sufficient bytes arrive from the server
        the method handle_read() will be called
    */
    void Read( int byte_count );

    /** write pre-defined message to server

        This is non-blocking, returning immediatly.
        When write completes
        the method handle_write() will be called
    */
    void Write();


private:
    boost::asio::io_service& myIOService;
    boost::asio::ip::tcp::tcp::socket * mySocketTCP;
    boost::asio::deadline_timer * myTimer;
    enum class constatus
    {
        no,                             /// there is no connection
        yes,                            /// connected
        not_yet                          /// Connection is being made, not yet complete
    }
    myConnection;
    unsigned char myRcvBuffer [ MAX_PACKET_SIZE_BYTES ];
    unsigned char myConnectMessage[15] {0x02, 0xfd, 00, 0x05, 00, 00, 00, 07, 0x0f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00};
    unsigned char myWriteMessage[15] {0x02, 0xfd, 0x80, 0x01, 00, 00, 00, 07, 0x0f, 0x0d, 0xAA, 0xBB, 0x22, 0x11, 0x22};

    void handle_read(
        const boost::system::error_code& error,
        std::size_t bytes_received );

    void handle_connect_write(
        const boost::system::error_code& error,
        std::size_t bytes_sent );

    void handle_write(
        const boost::system::error_code& error,
        std::size_t bytes_sent );
};

class cWorkSimulator
{
public:

    cWorkSimulator( boost::asio::io_service& io_service)
        : myTimer( new boost::asio::deadline_timer( io_service ))
        , myfStop( false )
    {

    }
    void StartWork()
    {
        // simulated work
        myTimer->expires_from_now(boost::posix_time::milliseconds(WORK_TIME_MSECS));

        myTimer->async_wait(boost::bind(&cWorkSimulator::FinishWork, this));
    }

    void FinishWork()
    {
        if( StopGet() )
        {
            std::cout << "Stopping\n";
            return;
        }
        if( ! myfWaitOnUser)
        {
            static int count;
            count++;
            std::cout << "Completed Job " << count << "\n";
        }

        // start another job
        StartWork();

    }
    void WaitOnUserSet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfWaitOnUser = true;
    }
    void WaitOnUserUnSet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfWaitOnUser = false;
    }
    bool WaitOnUserGet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        return myfWaitOnUser;
    }
    void Stop()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfStop = true;
    }
    bool StopGet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        return myfStop;
    }
private:
    boost::asio::deadline_timer * myTimer;
    std::mutex myMutex;
    bool myfWaitOnUser;
    bool myfStop;
};


/** Command handler receives commands from the keyboard monitor ( running in keyboard monitor thread )
    and dispatches them to the TCP client running in the main thread */

class cCommander
{
public:
    cCommander(
        boost::asio::io_service& io_service,
        cNonBlockingTCPClient& TCP )
        : myIOService( io_service )
        , myTCP( TCP )
        , myTimer( new boost::asio::deadline_timer( io_service ))
    {
        CheckForCommand();
    }

    /** Set command from user ( thread safe )

    This is called from the keyboard monitor in the keyboard monitor thread
    */
    void Command( const std::string& command);

    /** Get command from user ( thread safe )

    This is called from the main thread
    */
    string Command();


private:
    boost::asio::io_service& myIOService;
    boost::asio::deadline_timer * myTimer;
    cNonBlockingTCPClient & myTCP;
    std::string myCommand;
    std::mutex myMutex;

    /// Check for commands ( connect, read, write )
    void CheckForCommand();
};


/** Keyboard monitor

    Runs in its own thread

    'x<ENTER'        exit application
    's <Hz><ENTER>   change output buffer clock speed
*/
class cKeyboard
{
public:
    cKeyboard(
        boost::asio::io_service& io_service,
        cWorkSimulator& WS,
        cCommander& myCommander );

    void Start();

private:
    boost::asio::io_service& myIOService;
    cWorkSimulator* myWS;
    cCommander * myCommander;
};

cKeyboard::cKeyboard(
    boost::asio::io_service& io_service,
    cWorkSimulator& WS,
    cCommander& Commander
)
    : myIOService( io_service )
    , myWS( &WS )
    , myCommander( &Commander )
{
    // start monitor in own thread
    new std::thread(
        &cKeyboard::Start,
        std::ref(*this) );

    // allow time for thread to start and user to read usage instructions
    std::this_thread::sleep_for (std::chrono::seconds(3));
}


void cKeyboard::Start()
{
    std::cout << "\nKeyboard monitor running\n\n"
              "   To pause for user input type 'q<ENTER>\n"
              "   To connect to server type 'C <ip> <port><ENTER>\n"
              "   To read from server type 'R <byte count><ENTER>\n"
              "   To send a pre-defined message to the server type 'W'\n"
              "   To stop type 'x<ENTER>' ( DO NOT USE ctrlC )\n\n"
              "   Don't forget to hit <ENTER>!\n\n";

    std::string cmd;
    while( 1 )
    {
        getline( std::cin, cmd );
        std::cout << "input was " << cmd << "\n";
        switch( cmd[0] )
        {

        case 'x':
        case 'X':
            myCommander->Command( cmd );
            myWS->Stop();

            // return, ending the thread
            return;

        case 'q':
        case 'Q':
            std::cout << "Waiting for user input: C or R or W\n";
            myWS->WaitOnUserSet();
            break;

        case 'c':
        case 'C':
        case 'r':
        case 'R':
        case 'w':
        case 'W':

            // register command with TCP client
            myCommander->Command( cmd );

            // user input finished, resume work
            myWS->WaitOnUserUnSet();

            break;

        }
    }
}
void cCommander::CheckForCommand()
{
    string cmd = Command();
    if( cmd.length() )
    {
        std::cout << "cNonBlockingTCPClient::CheckForCommand " << cmd << "\n";

        std::stringstream sst(cmd);
        std::vector< std::string > vcmd;
        std::string a;
        while( getline( sst, a, ' ' ) )
            vcmd.push_back(a);

        switch( vcmd[0][0] )
        {
        case 'r':
        case 'R':
            if( vcmd.size() < 2 )
                std::cout << "Read command missing byte count\n";
            else
                myTCP.Read( atoi( vcmd[1].c_str()));
            break;

        case 'c':
        case 'C':
            myTCP.Connect( vcmd[1], vcmd[2] );
            break;

        case 'w':
        case 'W':
            myTCP.Write();
            break;

        case 'x':
        case 'X':
            // stop command, return without scheduling another check
            return;

        default:
            std::cout << "Unrecognized command\n";
            break;
        }

        // clear old command
        Command("");
    }

    //schedule next check
    myTimer->expires_from_now(boost::posix_time::milliseconds(500));

    myTimer->async_wait(boost::bind(&cCommander::CheckForCommand, this));
}

void cCommander::Command( const std::string& command)
{
    std::lock_guard<std::mutex> lck (myMutex);
    myCommand = command;
}
std::string cCommander::Command()
{
    std::lock_guard<std::mutex> lck (myMutex);
    return myCommand;
}

void cNonBlockingTCPClient::Connect(
    const std::string& ip,
    const std::string& port)
{
    try
    {
        boost::system::error_code ec;
        boost::asio::ip::tcp::tcp::resolver resolver( myIOService );
        boost::asio::ip::tcp::tcp::resolver::query query(
            ip,
            port );
        boost::asio::ip::tcp::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query,ec);
        if( ec )
            throw std::runtime_error("resolve");
        mySocketTCP = new boost::asio::ip::tcp::tcp::socket( myIOService );
        boost::asio::connect( *mySocketTCP, endpoint_iterator, ec );
        if ( ec || ( ! mySocketTCP->is_open() ) )
        {
            // connection failed
            delete mySocketTCP;
            mySocketTCP = 0;
            myConnection = constatus::no;
            std::cout << "Client Connection failed\n";

        }
        else
        {
            myConnection = constatus::yes;
            std::cout << "Client Connected OK\n";

            boost::asio::async_write(
                *mySocketTCP,
                boost::asio::buffer(myConnectMessage, 15),
                boost::bind(&cNonBlockingTCPClient::handle_connect_write, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred ));
        }
    }

    catch ( ... )
    {
        std::cout << "Client Connection failed 2\n";
    }
}
void cNonBlockingTCPClient::Read( int byte_count )
{
    if( myConnection != constatus::yes )
    {
        std::cout << "Read Request but no connection\n";
        return;
    }
    if( byte_count < 1 )
    {
        std::cout << "Error in read command\n";
    }
    if( byte_count > MAX_PACKET_SIZE_BYTES )
    {
        std::cout << "Too many bytes requested\n";
        return;
    }
    async_read(
        * mySocketTCP,
        boost::asio::buffer(myRcvBuffer, byte_count ),
        boost::bind(&cNonBlockingTCPClient::handle_read, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred ));
    std::cout << "waiting for server to reply\n";
}

void cNonBlockingTCPClient::Write()
{
    if( myConnection != constatus::yes )
    {
        std::cout << "Write Request but no connection\n";
        return;
    }
    boost::asio::async_write(
        *mySocketTCP,
        boost::asio::buffer(myWriteMessage, 15),
        boost::bind(&cNonBlockingTCPClient::handle_write, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred ));
}

void cNonBlockingTCPClient::handle_read(
    const boost::system::error_code& error,
    std::size_t bytes_received )
{
    if( error )
    {
        std::cout << "Connection closed\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << bytes_received << " bytes read\n";
    for( int k = 0; k < bytes_received; k++ )
        std::cout << std::hex << (int)myRcvBuffer[k] << " ";
    std::cout << std::dec << "\n";
}

void cNonBlockingTCPClient::handle_connect_write(
    const boost::system::error_code& error,
    std::size_t bytes_sent )
{
    if( error || bytes_sent != 15 )
    {
        std::cout << "Error sending connection message to server\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << "Connection message sent to server\n";
}

void cNonBlockingTCPClient::handle_write(
    const boost::system::error_code& error,
    std::size_t bytes_sent )
{
    if( error || bytes_sent != 15 )
    {
        std::cout << "Error sending write message to server\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << "Write message sent to server\n";
}

int main()
{
    // construct event manager
    boost::asio::io_service io_service;

    // construct work simulator
    cWorkSimulator theWorkSimulator( io_service );

    // construct TCP client
    cNonBlockingTCPClient theClient( io_service );

    // construct commander to dispatch commands from user in keyboard thread to TCP client in main thread
    cCommander theCommander(
        io_service,
        theClient );

    // start keyboard monitor
    cKeyboard theKeyBoard(
        io_service,
        theWorkSimulator,
        theCommander
    );

    // start simulating work
    theWorkSimulator.StartWork();

    // start event handler ( runs until stop requested )
    io_service.run();

    std::cout << "Event manager finished\n";

    return 0;
}
