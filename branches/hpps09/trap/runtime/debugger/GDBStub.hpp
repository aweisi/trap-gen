/***************************************************************************\
 *
 *
 *            ___        ___           ___           ___
 *           /  /\      /  /\         /  /\         /  /\
 *          /  /:/     /  /::\       /  /::\       /  /::\
 *         /  /:/     /  /:/\:\     /  /:/\:\     /  /:/\:\
 *        /  /:/     /  /:/~/:/    /  /:/~/::\   /  /:/~/:/
 *       /  /::\    /__/:/ /:/___ /__/:/ /:/\:\ /__/:/ /:/
 *      /__/:/\:\   \  \:\/:::::/ \  \:\/:/__\/ \  \:\/:/
 *      \__\/  \:\   \  \::/~~~~   \  \::/       \  \::/
 *           \  \:\   \  \:\        \  \:\        \  \:\
 *            \  \ \   \  \:\        \  \:\        \  \:\
 *             \__\/    \__\/         \__\/         \__\/
 *
 *
 *
 *
 *   This file is part of TRAP.
 *
 *   TRAP is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *   or see <http://www.gnu.org/licenses/>.
 *
 *
 *
 *   (c) Luca Fossati, fossati@elet.polimi.it
 *
\***************************************************************************/

/**
 * This file contains the methods necessary to communicate with GDB in
 * order to debug software running on simulators. Source code takes inspiration
 * from the linux kernel (sparc-stub.c) and from ac_gdb.H in the ArchC sources
 */

#ifndef GDBSTUB_HPP
#define GDBSTUB_HPP

#include <csignal>
#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif

#include <systemc.h>

#include <vector>
#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>

#include "utils.hpp"

#include "ABIIf.hpp"
#include "ToolsIf.hpp"

#include "BreakpointManager.hpp"
#include "GDBConnectionManager.hpp"

template<class issueWidth> class GDBStub : public ToolsIf<issueWidth>, public sc_module{
  private:
    enum stopType {BREAK_stop=0, STEP_stop, SEG_stop, TIMEOUT_stop, PAUSED_stop, UNK_stop};
    ///Thread used to send and receive responses with the GDB debugger
    struct GDBThread{
        GDBStub<issueWidth> &gdbStub;
        GDBThread(GDBStub<issueWidth> &gdbStub) : gdbStub(gdbStub){}
        void operator()(){
            while(!gdbStub.isKilled){
                if(gdbStub.connManager.checkInterrupt()){
                    gdbStub.step = 2;
                }
                else{
                    //First of all I have to perform some cleanup
                    gdbStub.breakManager.clearAllBreaks();
                    gdbStub.step = 0;
                    gdbStub.isConnected = false;
                    break;
                }
            }
        }
    };

    ///Manages connection among the GDB target stub (this class) and
    ///the GDB debugger
    GDBConnectionManager connManager;
    ///Interface for communication with the internal processor's structure
    ABIIf<issueWidth> &processorInstance;
    ///Handles the breakpoints which have been set in the system
    BreakpointManager<issueWidth> breakManager;
    ///Determines whether the processor has to halt as a consequence of a
    ///step command
    unsigned int step;
    ///Keeps track of the last breakpoint encountered by this processor
    Breakpoint<issueWidth> * breakReached;
    ///Specifies whether the watchpoints and breakpoints are enabled or not
    bool breakEnabled;
    ///Specifies whether GDB server side killed the simulation
    bool isKilled;
    ///In case we decided to run the simulation only for a limited ammount of time
    ///this variable contains that time
    double timeToGo;
    ///In case we decided to jump onwards or backwards for a specified ammount of time,
    ///this variable contains that time
    double timeToJump;
    ///In case the simulation is run only for a specified ammount of time,  this variable
    ///contains the simulation time at that start time
    double simStartTime;
    ///Specifies that we have to stop because a timeout was encountered
    bool timeout;
    ///Event used to manage execution for a specified ammount of time
    sc_event pauseEvent;
    ///Condition used to stop processor execution until simulation is restarted
    boost::condition gdbPausedEvent;
    ///Mutex used to access the condition
    boost::mutex global_mutex;
    ///Sepecifies if GDB is connected to this stub or not
    bool isConnected;
    ///Specifies that the first run is being made
    bool firstRun;

    /********************************************************************/
    ///Checks if a breakpoint is present at the current address and
    ///in case it halts execution
    #ifndef NDEBUG
    inline void checkBreakpoint(const issueWidth &address){
    #else
    inline void checkBreakpoint(const issueWidth &address) throw(){
    #endif
        if(this->breakEnabled && this->breakManager.hasBreakpoint(address)){
            this->breakReached = this->breakManager.getBreakPoint(address);
            if(breakReached == NULL){
                THROW_EXCEPTION("I stopped because of a breakpoint, but no breakpoint was found");
            }
            this->setStopped(BREAK_stop);
        }
    }
    ///Checks if execution must be stopped because of a step command
    inline void checkStep() throw(){
        if(this->step == 1)
            this->step++;
        else if(this->step == 2){
            this->step = 0;
            if(this->timeout){
                this->timeout = false;
                this->setStopped(TIMEOUT_stop);
            }
            else
                this->setStopped(STEP_stop);
        }
    }

    ///Starts the thread which will manage the connection with the
    ///GDB debugger
    void startThread(){
        GDBThread thread(*this);
        boost::thread th(thread);
    }

    ///This method is called when we need asynchronously halt the
    ///execution of the processor this instance of the stub is
    ///connected to; it is usually used when a processor is halted
    ///and it has to communicated to the other processor that they have
    ///to halt too; note that this method halts SystemC execution and
    ///it also starts new threads (one for each processor) which will
    ///deal with communication with the stub; when a continue or
    ///step signal is met, then the receving thread is killed. When
    ///all the threads are killed execution can be resumed (this means
    ///that all GDBs pressed the resume button)
    ///This method is also called at the beginning of the simulation by
    ///the first processor which starts execution
    void setStopped(stopType stopReason = UNK_stop){
        //saving current simulation time
        double curSimTime = sc_time_stamp().to_double();

        //Now I have to behave differently depending on whether database support is enabled or not
        //if it is enabled I do not stop simulation,  while if it is not enabled I have to stop simulation in
        //order to be able to inspect the content of the processor - memory - etc...
        //Computing the next simulation time instant
        if(this->timeToGo > 0){
            this->timeToGo -= (curSimTime - this->simStartTime);
            if(this->timeToGo < 0)
                this->timeToGo = 0;
            this->simStartTime = curSimTime;
        }
        //Disabling break and watch points
        this->breakEnabled = false;
        this->awakeGDB(stopReason);
        //pausing simulation
/*        if(stopReason != TIMEOUT && stopReason !=  PAUSED){
            boost::mutex::scoped_lock lk(this->global_mutex);
            this->gdbPausedEvent.wait(lk);
        }*/
        while(this->waitForRequest())
            ;
    }

    ///Sends a TRAP message to GDB so that it is awaken
    void awakeGDB(stopType stopReason = UNK_stop){
        switch(stopReason){
            case STEP_stop:{
                GDBResponse response;
                response.type = GDBResponse::S_rsp;
                response.payload = SIGTRAP;
                this->connManager.sendResponse(response);
            break;}
            case BREAK_stop:{
                //Here I have to determine the case of the breakpoint: it may be a normal
                //breakpoint placed on an instruction or it may be a watch on a variable
                if(this->breakReached == NULL){
                    THROW_EXCEPTION("I stopped because of a breakpoint, but it is NULL");
                }

                if(this->breakReached->type == Breakpoint<issueWidth>::HW_break ||
                            this->breakReached->type == Breakpoint<issueWidth>::MEM_break){
                    GDBResponse response;
                    response.type = GDBResponse::S_rsp;
                    response.payload = SIGTRAP;
                    this->connManager.sendResponse(response);
                }
                else{
                    GDBResponse response;
                    response.type = GDBResponse::T_rsp;
                    response.payload = SIGTRAP;
                    std::pair<std::string, unsigned int> info;
                    info.second = this->breakReached->address;
                    switch(this->breakReached->type){
                        case Breakpoint<issueWidth>::WRITE_break:
                            info.first = "watch";
                        break;
                        case Breakpoint<issueWidth>::READ_break:
                            info.first = "rwatch";
                        break;
                        case Breakpoint<issueWidth>::ACCESS_break:
                            info.first = "awatch";
                        break;
                        default:
                            info.first = "none";
                        break;
                    }
                    response.size = sizeof(issueWidth);
                    response.info.push_back(info);
                    this->connManager.sendResponse(response);
                }
            break;}
            case SEG_stop:{
                //An error has occurred during processor execution (illelgal instruction, reading out of memory, ...);
                GDBResponse response;
                response.type = GDBResponse::S_rsp;
                response.payload = SIGILL;
                this->connManager.sendResponse(response);
            break;}
            case TIMEOUT_stop:{
                //the simulation time specified has elapsed,  so simulation halted
                GDBResponse resp;
                resp.type = GDBResponse::OUTPUT_rsp;
                resp.message = "Specified Simulation time completed - Current simulation time: " + sc_time_stamp().to_string() + " (ps)\n";
                this->connManager.sendResponse(resp);
                this->connManager.sendInterrupt();
            break;}
            case PAUSED_stop:{
                //the simulation time specified has elapsed, so simulation halted
                GDBResponse resp;
                resp.type = GDBResponse::OUTPUT_rsp;
                resp.message = "Simulation Paused - Current simulation time: " + sc_time_stamp().to_string() + " (ps)\n";
                this->connManager.sendResponse(resp);
                this->connManager.sendInterrupt();
            break;}
            default:
                this->connManager.sendInterrupt();
            break;
        }
    }

    ///Signals to the GDB debugger that simulation ended; the error variable specifies
    ///if the program ended with an error
    void signalProgramEnd(bool error = false){
        if(!this->isKilled || error){
            GDBResponse response;
            //Now I just print a message to the GDB console signaling the user that the program is ending
            if(error){
                //I start anyway by signaling an error
                GDBResponse rsp;
                rsp.type = GDBResponse::ERROR_rsp;
                this->connManager.sendResponse(rsp);
            }
            response.type = GDBResponse::OUTPUT_rsp;
            if(error){
                response.message = "Program Ended With an Error\n";
            }
            else
                response.message = "Program Correctly Ended\n";
            this->connManager.sendResponse(response);

            //Now I really communicate to GDB that the program ended
            response.type = GDBResponse::W_rsp;
            if(error)
                response.payload = SIGABRT;
            else
                response.payload = SIGQUIT;
            this->connManager.sendResponse(response);
        }
    }

    ///Waits for an incoming request by the GDB debugger and, once it
    ///has been received, it routes it to the appropriate handler
    ///Returns whether we must be listening for other incoming data or not
    bool waitForRequest(){
        GDBRequest req = connManager.processRequest();
        switch(req.type){
            case GDBRequest::QUEST_req:
                //? request: it asks the target the reason why it halted
                return this->reqStopReason();
            break;
            case GDBRequest::EXCL_req:
                // ! request: it asks if extended mode is supported
                return this->emptyAction(req);
            break;
            case GDBRequest::c_req:
                //c request: Continue command
                return this->cont(req);
            break;
            case GDBRequest::C_req:
                //C request: Continue with signal command, currently not supported
                return this->emptyAction(req);
            break;
            case GDBRequest::D_req:
                //D request: disconnection from the remote target
                return this->detach(req);
            break;
            case GDBRequest::g_req:
                //g request: read general register
                return this->readRegisters();
            break;
            case GDBRequest::G_req:
                //G request: write general register
                return this->writeRegisters(req);
            break;
            case GDBRequest::H_req:
                //H request: multithreading stuff, not currently supported
                return this->emptyAction(req);
            break;
            case GDBRequest::i_req:
                //i request: single clock cycle step; currently it is not supported
                //since it requires advancing systemc by a specified ammont of
                //time equal to the clock cycle (or one of its multiple) and I still
                //have to think how to know the clock cycle of the processor and
                //how to awake again all the processors after simulation stopped again
                return this->emptyAction(req);
            break;
            case GDBRequest::I_req:
                //i request: signal and single clock cycle step
                return this->emptyAction(req);
            break;
            case GDBRequest::k_req:
                //i request: kill application: I simply call the sc_stop method
                return this->killApp();
            break;
            case GDBRequest::m_req:
                //m request: read memory
                return this->readMemory(req);
            break;
            case GDBRequest::M_req:
            case GDBRequest::X_req:
                //M request: write memory
                return this->writeMemory(req);
            break;
            case GDBRequest::p_req:
                //p request: register read
                return this->readRegister(req);
            break;
            case GDBRequest::P_req:
                //P request: register write
                return this->writeRegister(req);
            break;
            case GDBRequest::q_req:
                //P request: register write
                return this->genericQuery(req);
            break;
            case GDBRequest::s_req:
                //s request: single step
                return this->doStep(req);
            break;
            case GDBRequest::S_req:
                //S request: single step with signal
                return this->emptyAction(req);
            break;
            case GDBRequest::t_req:
                //t request: backward search: currently not supported
                return this->emptyAction(req);
            break;
            case GDBRequest::T_req:
                //T request: thread stuff: currently not supported
                return this->emptyAction(req);
            break;
            case GDBRequest::z_req:
                //z request: breakpoint/watch removal
                return this->removeBreakpoint(req);
            break;
            case GDBRequest::Z_req:
                //z request: breakpoint/watch addition
                return this->addBreakpoint(req);
            break;
            case GDBRequest::INTR_req:
                //received an iterrupt from GDB: I pause simulation and signal GDB that I stopped
                return this->recvIntr();
            break;
            case GDBRequest::ERROR_req:
                std::cerr << "Error in the connection with the GDB debugger, connection will be terminated" << std::endl;
                this->isConnected = false;
                this->resumeExecution();
                this->breakEnabled = false;
                return false;
            break;
            default:
                return this->emptyAction(req);
            break;
        }
    }

    ///Method used to resume execution after GDB has issued
    ///the continue or step signal
    void resumeExecution(){
        //I'm going to restart execution, so I can again enable watch and break points
        this->breakEnabled = true;
        this->simStartTime = sc_time_stamp().to_double();
        //this->gdbPausedEvent.notify_all();
        if(this->timeToGo > 0){
            this->pauseEvent.notify(sc_time(this->timeToGo, SC_PS));
        }
    }

    /** Here start all the methods to handle the different GDB requests **/

    ///It does nothing, it simply sends an empty string back to the
    ///GDB debugger
    bool emptyAction(GDBRequest &req){
        GDBResponse resp;
        resp.type = GDBResponse::NOT_SUPPORTED_rsp;
        this->connManager.sendResponse(resp);
        return true;
    }

    ///Asks for the reason why the processor is stopped
    bool reqStopReason(){
        this->awakeGDB();
        return true;
    }

    ///Reads the value of a register;
    bool readRegister(GDBRequest &req){
        GDBResponse rsp;
        rsp.type = GDBResponse::REG_READ_rsp;
        try{
            if(req.reg < this->processorInstance.nGDBRegs()){
                issueWidth regContent = this->processorInstance.readGDBReg(req.reg);
                this->valueToBytes(rsp.data, regContent);
            }
            else{
                this->valueToBytes(rsp.data, 0);
            }
        }
        catch(...){
            this->valueToBytes(rsp.data, 0);
        }

        this->connManager.sendResponse(rsp);
        return true;
    }

    ///Reads the value of a memory location
    bool readMemory(GDBRequest &req){
        GDBResponse rsp;
        rsp.type = GDBResponse::MEM_READ_rsp;

        for(unsigned int i = 0; i < req.length; i++){
            try{
                unsigned char memContent = this->processorInstance.readMem(req.address + i);
                this->valueToBytes(rsp.data, memContent);
            }
            catch(...){
                this->valueToBytes(rsp.data, 0);
            }
        }

        this->connManager.sendResponse(rsp);
        return true;
    }

    bool cont(GDBRequest &req){
        if(req.address != 0){
            this->processorInstance.setPC(req.address);
        }

        //Now, I have to restart SystemC, since the processor
        //has to go on; note that actually SystemC restarts only
        //after all the gdbs has issued some kind of start command
        //(either a continue, a step ...)
        this->resumeExecution();
        return false;
    }

    bool detach(GDBRequest &req){
        //First of all I have to perform some cleanup
        this->breakManager.clearAllBreaks();
        //Finally I can send a positive response
        GDBResponse resp;
        resp.type = GDBResponse::OK_rsp;
        this->connManager.sendResponse(resp);
        this->step = 0;
        this->isConnected = false;
        this->resumeExecution();
        this->breakEnabled = false;
        return false;
    }

    bool readRegisters(){
        //I have to read all the general purpose registers and
        //send their content back to GDB
        GDBResponse resp;
        resp.type = GDBResponse::REG_READ_rsp;
        for(unsigned int i = 0; i < this->processorInstance.nGDBRegs(); i++){
            try{
                issueWidth regContent = this->processorInstance.readGDBReg(i);
                this->valueToBytes(resp.data, regContent);
            }
            catch(...){
                this->valueToBytes(resp.data, 0);
            }
        }
        this->connManager.sendResponse(resp);
        return true;
    }

    bool writeRegisters(GDBRequest &req){
        std::vector<issueWidth> regContent;
        this->bytesToValue(req.data, regContent);
        typename std::vector<issueWidth>::iterator dataIter, dataEnd;
        bool error = false;
        unsigned int i = 0;
        for(dataIter = regContent.begin(), dataEnd = regContent.end();
                                        dataIter != dataEnd; dataIter++){
            try{
                this->processorInstance.setGDBReg(*dataIter, i);
            }
            catch(...){
                error = true;
            }
            i++;
        }

        GDBResponse resp;

        if(i != (unsigned int)this->processorInstance.nGDBRegs() || error)
            resp.type = GDBResponse::ERROR_rsp;
        else
            resp.type = GDBResponse::OK_rsp;
        this->connManager.sendResponse(resp);
        return true;
    }

    bool writeMemory(GDBRequest &req){
        bool error = false;
        unsigned int bytes = 0;
        std::vector<char>::iterator dataIter, dataEnd;
        for(dataIter = req.data.begin(), dataEnd = req.data.end(); dataIter != dataEnd; dataIter++){
            try{
                this->processorInstance.writeMem(req.address + bytes, *dataIter);
                bytes++;
            }
            catch(...){
                error = true;
                break;
            }
        }

        GDBResponse resp;
        resp.type = GDBResponse::OK_rsp;

        if(bytes != (unsigned int)req.length || error){
            resp.type = GDBResponse::ERROR_rsp;
        }

        this->connManager.sendResponse(resp);
        return true;
    }

    bool writeRegister(GDBRequest &req){
        GDBResponse rsp;
        if(req.reg <= this->processorInstance.nGDBRegs()){
            try{
                this->processorInstance.setGDBReg(req.value, req.reg);
                rsp.type = GDBResponse::OK_rsp;
            }
            catch(...){
                rsp.type = GDBResponse::ERROR_rsp;
            }
        }
        else{
            rsp.type = GDBResponse::ERROR_rsp;        //First of all I have to perform some cleanup
        this->breakManager.clearAllBreaks();
        //Finally I can send a positive response
        GDBResponse resp;
        resp.type = GDBResponse::OK_rsp;
        this->connManager.sendResponse(resp);
        this->step = 0;
        this->resumeExecution();
        this->isConnected = false;

        }
        this->connManager.sendResponse(rsp);
        return true;
    }

    bool killApp(){
        this->isKilled = true;
        sc_stop();
        wait();
        return true;
    }

    bool doStep(GDBRequest &req){
        if(req.address != 0){
            this->processorInstance.setPC(req.address);
        }

        this->step = 1;
        this->resumeExecution();
        return false;
    }

    bool recvIntr(){
        this->breakManager.clearAllBreaks();
        this->step = 0;
        this->isConnected = false;
        return true;
    }

    bool addBreakpoint(GDBRequest &req){
        GDBResponse resp;
        switch(req.value){
            /*case 0:
                if(this->breakManager.addBreakpoint(Breakpoint<issueWidth>::MEM, req.address, req.length))
                resp.type = GDBResponse::OK;
                else
                resp.type = GDBResponse::ERROR;
            break;*/
            case 0:
            case 1:
                if(this->breakManager.addBreakpoint(Breakpoint<issueWidth>::HW_break, req.address, req.length))
                    resp.type = GDBResponse::OK_rsp;
                else
                    resp.type = GDBResponse::ERROR_rsp;
            break;
            case 2:
                if(this->breakManager.addBreakpoint(Breakpoint<issueWidth>::WRITE_break, req.address, req.length))
                    resp.type = GDBResponse::OK_rsp;
                else
                    resp.type = GDBResponse::ERROR_rsp;
            break;
            case 3:
                if(this->breakManager.addBreakpoint(Breakpoint<issueWidth>::READ_break, req.address, req.length))
                    resp.type = GDBResponse::OK_rsp;
                else
                    resp.type = GDBResponse::ERROR_rsp;
            break;
            case 4:
                if(this->breakManager.addBreakpoint(Breakpoint<issueWidth>::ACCESS_break, req.address, req.length))
                    resp.type = GDBResponse::OK_rsp;
                else
                    resp.type = GDBResponse::ERROR_rsp;
            break;
            default:
                resp.type = GDBResponse::NOT_SUPPORTED_rsp;
            break;
        }
        this->connManager.sendResponse(resp);
        return true;
    }

    bool removeBreakpoint(GDBRequest &req){
        GDBResponse resp;
        if(this->breakManager.removeBreakpoint(req.address))
            resp.type = GDBResponse::OK_rsp;
        else
            resp.type = GDBResponse::ERROR_rsp;
        this->connManager.sendResponse(resp);
        return true;
    }

    bool genericQuery(GDBRequest &req){
        //I have to determine the query packet; in case it is Rcmd I deal with it
        GDBResponse resp;
        if(req.command != "Rcmd")
            resp.type = GDBResponse::NOT_SUPPORTED_rsp;
        else{
            //lets see which is the custom command being sent
            std::string::size_type spacePos = req.extension.find(' ');
            std::string custComm;
            if(spacePos == std::string::npos)
                custComm = req.extension;
            else
                custComm = req.extension.substr(0,  spacePos);
            if(custComm == "go"){
                //Ok,  finally I got the right command: lets see for
                //how many nanoseconds I have to execute the continue
                this->timeToGo = atof(req.extension.substr(spacePos + 1).c_str())*1e3;
                if(this->timeToGo < 0){
                    resp.type = GDBResponse::OUTPUT_rsp;
                    resp.message = "Please specify a positive offset";
                    this->connManager.sendResponse(resp);
                    resp.type = GDBResponse::NOT_SUPPORTED_rsp;
                    this->timeToGo = 0;
                }
                else
                    resp.type = GDBResponse::OK_rsp;
            }
            else if(custComm == "go_abs"){
                //This command specify to go up to a specified simulation time; the time is specified in nanoseconds
                this->timeToGo = atof(req.extension.substr(spacePos + 1).c_str())*1e3 - sc_time_stamp().to_double();
                if(this->timeToGo < 0){
                    resp.type = GDBResponse::OUTPUT_rsp;
                    resp.message = "Please specify a positive offset";
                    this->connManager.sendResponse(resp);
                    resp.type = GDBResponse::NOT_SUPPORTED_rsp;
                    this->timeToGo = 0;
                }
                else{
                    resp.type = GDBResponse::OK_rsp;
                }
            }
            else if(custComm == "status"){
                 //Returns the current status of the STUB
                 resp.type = GDBResponse::OUTPUT_rsp;
                 resp.message = "Current simulation time: " + boost::lexical_cast<std::string>(sc_time_stamp().to_double()) + " (ps)\n";
                 if(this->timeToGo != 0)
                    resp.message += "Simulating for : " + boost::lexical_cast<std::string>(this->timeToGo) + " Nanoseconds\n";
                 this->connManager.sendResponse(resp);
                 resp.type = GDBResponse::OK_rsp;
            }
            else if(custComm == "time"){
                //This command is simply a query to know the current simulation time
                resp.type = GDBResponse::OUTPUT_rsp;
                 resp.message = "Current simulation time: " + boost::lexical_cast<std::string>(sc_time_stamp().to_double()) + " (ps)\n";
                 this->connManager.sendResponse(resp);
                 resp.type = GDBResponse::OK_rsp;
            }
            else if(custComm == "help"){
                //This command is simply a query to know the current simulation time
                resp.type = GDBResponse::OUTPUT_rsp;
                 resp.message = "Help about the custom GDB commands available for the ReSP simulation platform:\n";
                 resp.message += "   monitor help:       prints the current message\n";
                 resp.message += "   monitor time:       returns the current simulation time\n";
                 resp.message += "   monitor status:     returns the status of the simulation\n";
                 resp.message += "   monitor go n:       after the \'continue\' command is given, it simulates for n (ns) starting from the current time\n";
                 resp.message += "   monitor go_abs n:   after the \'continue\' command is given, it simulates up to instant n (ns)\n";
                 this->connManager.sendResponse(resp);
                 resp.type = GDBResponse::OK_rsp;
            }
            else{
                resp.type = GDBResponse::NOT_SUPPORTED_rsp;
            }
        }
        this->connManager.sendResponse(resp);
        return true;
    }

    ///Separates the bytes which form an integer value and puts them
    ///into an array of bytes
    template <class ValueType> void valueToBytes(std::vector<char> &byteHolder, ValueType value){
        if(this->processorInstance.matchEndian()){
            for(unsigned int i = 0; i < sizeof(ValueType); i++){
                byteHolder.push_back((char)((value & (0x0FF << 8*i)) >> 8*i));
            }
        }
        else{
            for(int i = sizeof(ValueType) - 1; i >= 0; i--){
                byteHolder.push_back((char)((value & (0x0FF << 8*i)) >> 8*i));
            }
        }
    }

    ///Converts a vector of bytes into a vector of integer values
    void bytesToValue(std::vector<char> &byteHolder, std::vector<issueWidth> &values){
        for(unsigned int i = 0; i < byteHolder.size(); i += sizeof(issueWidth)){
            issueWidth buf = 0;
            for(unsigned int k = 0; k < sizeof(issueWidth); k++){
                buf |= (byteHolder[i + k] << 8*k);
            }
            values.push_back(buf);
        }
    }

  public:
    SC_HAS_PROCESS(GDBStub);
    GDBStub(ABIIf<issueWidth> &processorInstance) :
                    sc_module("debugger"), connManager(processorInstance.matchEndian()), processorInstance(processorInstance),
                step(0), breakReached(NULL), breakEnabled(true), isKilled(false), timeout(false), isConnected(false),
                timeToGo(0), timeToJump(0), simStartTime(0), firstRun(true){
        SC_METHOD(pauseMethod);
        sensitive << this->pauseEvent;
        dont_initialize();

        end_module();
    }
    ///Method used to pause simulation
    void pauseMethod(){
        this->step = 2;
        this->timeout = true;
    }
    ///Overloading of the end_of_simulation method; it can be used to execute methods
    ///at the end of the simulation
    void end_of_simulation(){
        if(this->isConnected){
            this->isKilled = false;
            this->signalProgramEnd();
        }
    }
    ///Starts the connection with the GDB client
    void initialize(unsigned int port = 1500){
        this->connManager.initialize(port);
        this->isConnected = true;
        //Now I have to listen for incoming GDB messages; this will
        //be done in a new thread.
        this->startThread();
    }
    ///Method called at every cycle from the processor's main loop
    bool newIssue(const issueWidth &curPC, const void *curInstr) throw(){
        if(this->firstRun){
            this->firstRun = false;
            this->breakEnabled = false;
            while(this->waitForRequest())
                ;
/*            boost::mutex::scoped_lock lk(this->global_mutex);
            this->gdbPausedEvent.wait(lk);*/
        }
        else{
            this->checkStep();
            this->checkBreakpoint(curPC);
        }
        return false;
    }
};

#endif