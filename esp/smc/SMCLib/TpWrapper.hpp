/*##############################################################################

    Copyright (C) 2011 HPCC Systems.

    All rights reserved. This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
############################################################################## */

// TpWrapper.hpp: interface for the CTpWrapper class.
//
//////////////////////////////////////////////////////////////////////

#ifndef _ESPWIZ_TpWrapper_HPP__
#define _ESPWIZ_TpWrapper_HPP__

#ifdef _WIN32
    #ifdef SMCLIB_EXPORTS
        #define TPWRAPPER_API __declspec(dllexport)
    #else
        #define TPWRAPPER_API __declspec(dllimport)
    #endif
#else
    #define TPWRAPPER_API
#endif



#include "jiface.hpp"
#include "mpbase.hpp"
#include "daclient.hpp"
#include "dadfs.hpp"
#include "dafdesc.hpp"
#include "dasds.hpp"
#include "danqs.hpp"
#include "environment.hpp"
#include "ws_topology.hpp"
#include <string>
#include <set>


using std::set;
using std::string;

#define eqClusters  "TpClusters"
#define eqCluster   "TpCluster"
#define eqComputers "TpComputers"
#define eqComputer  "TpComputer"

#define eqGroups    "TpGroups"
#define eqGroup "TpGroup"
#define eqGroupName  "Name"

#define eqPrefixNode "Node"
#define eqPrefixName "Prefix"

#define eqClusterName  "Name"
#define eqClusterDescription  "Desc"
#define eqClusterPath  "Path"
#define eqClusterType  "Type"
#define eqClusterDataDirectory  "Directory"


#define eqMachineName  "Name"
#define eqMachinePath  "Path"
#define eqMachineAddress  "Netaddress"
#define eqMachineType  "Type"

#define eqMachineUserID     "UserID"
#define eqMachinePassword   "Password"
#define eqMachineDomain     "Domain"
#define eqMachineOS         "OS"

#define eqMachineAvailablability    "Available"

#define eqTHORMACHINES          "THORMACHINES"
#define eqHOLEMACHINES          "HOLEMACHINES"
#define eqROXIEMACHINES       "ROXIEMACHINES"
#define eqMACHINES              "MACHINES"


#define eqDali                  "DaliServerProcess"
#define eqEclServer         "EclServerProcess"
#define eqEclCCServer       "EclCCServerProcess"
#define eqEclScheduler      "EclSchedulerProcess"
#define eqEclAgent          "EclAgentProcess"
#define eqAgentExec         "AgentExecProcess"
#define eqEsp                   "EspProcess"
#define eqDfu                   "DfuServerProcess"
#define eqDkcSlave          "DKCSlaveProcess"
#define eqSashaServer      "SashaServerProcess"
#define eqLdapServer       "LDAPServerProcess"
#define eqFTSlave          "FTSlaveProcess"
#define eqGenesisServer     "GenesisServerProcess"


#define eqDropZone              "DropZone"

#define eqAVAILABLEMACHINES     "AVAILABLEMACHINES"
#define eqRootNode  "ROOT"

#define eqAllClusters "ALLCLUSTERS"
#define eqAllNodes    "ALLNODES"
#define eqAllServices "ALLSERVICES"


#define eqHoleCluster  "HoleCluster"
#define eqThorCluster  "ThorCluster"
#define eqRoxieCluster "RoxieCluster"

#define eqThorMasterProcess     "ThorMasterProcess"
#define eqThorSlaveProcess      "ThorSlaveProcess"
#define eqThorSpareProcess      "ThorSpareProcess"
#define eqHoleSocketProcess     "HoleSocketProcess"
#define eqHoleProcessorProcess  "HoleProcessorProcess"
#define eqHoleControlProcess    "HoleControlProcess"
#define eqHoleCollatorProcess   "HoleCollatorProcess"
#define eqHoleStandbyProcess    "HoleStandbyProcess"

#define SDS_LOCK_TIMEOUT 30000


class TPWRAPPER_API CTpWrapper : public CInterface  
{
    
private:
    void setAttPath(StringBuffer& Path,const char* PathToAppend,const char* AttName,const char* AttValue,StringBuffer& returnStr);
    void getAttPath(const char* Path,StringBuffer& returnStr);
    bool ContainsProcessDefinition(IPropertyTree& node,const char* clusterName);
    const char* getNodeNameTag(const char* MachineType);
   void fetchInstances(const char* ServiceType, IPropertyTree& service, IArrayOf<IEspTpMachine>& tpMachines);
    
public:
    IMPLEMENT_IINTERFACE;
    CTpWrapper() {};
    virtual ~CTpWrapper() {};
    void getClusterInfo(const char* Cluster,StringBuffer& returnStr);
    bool getClusterLCR(const char* clusterType, const char* clusterName);
    void getClusterProcessList(const char* ClusterType, IArrayOf<IEspTpCluster>& clusters, bool ignoreduplicatqueues=false, bool ignoreduplicategroups=false);
    void getHthorClusterList(IArrayOf<IEspTpCluster>& clusterList);
    void getGroupList(IArrayOf<IEspTpGroup> &Groups);
    void getCluster(const char* ClusterType,IPropertyTree& returnRoot);
    void getClusterMachineList(const char* ClusterType,const char* ClusterPath, const char* ClusterDirectory, 
                                        IArrayOf<IEspTpMachine> &MachineList, bool& hasThorSpareProcess);
    void getMachineList( const char* MachineType,
                        const char* MachinePath,
                        const char* Status,
                                const char* Directory,
                        IArrayOf<IEspTpMachine> &MachineList, 
                        set<string>* pMachineNames=NULL);
    void getDropZoneList(const char* MachineType, const char* MachinePath, const char* Directory, IArrayOf<IEspTpMachine> &MachineList);
    void setMachineInfo(const char* name,const char* type,IEspTpMachine& machine);
    void resolveGroupInfo(const char* groupName,StringBuffer& Cluster, StringBuffer& ClusterPrefix);
    void getMachineInfo(IEspTpMachine& machineInfo,IPropertyTree& machine,const char* ParentPath,const char* MachineType,const char* nodenametag);

    void getTpDaliServers(IArrayOf<IConstTpDali>& list);
    void getTpEclServers(IArrayOf<IConstTpEclServer>& ServiceList);
    void getTpEclCCServers(IArrayOf<IConstTpEclServer>& ServiceList, const char* name = NULL);
    void getTpEclAgents(IArrayOf<IConstTpEclAgent>& list, const char* name = NULL);
    void getTpEclSchedulers(IArrayOf<IConstTpEclScheduler>& ServiceList, const char* name = NULL);
    void getTpEspServers(IArrayOf<IConstTpEspServer>& list);
    void getTpDfuServers(IArrayOf<IConstTpDfuServer>& list);
    void getTpSashaServers(IArrayOf<IConstTpSashaServer>& list);
    void getTpLdapServers(IArrayOf<IConstTpLdapServer>& list);
    void getTpDropZones(IArrayOf<IConstTpDropZone>& list);
    void getTpFTSlaves(IArrayOf<IConstTpFTSlave>& list);
    void getTpDkcSlaves(IArrayOf<IConstTpDkcSlave>& list);
    void getTpGenesisServers(IArrayOf<IConstTpGenesisServer>& list);

    void queryTargetClusters(double version, const char* clusterType, const char* clusterName, IArrayOf<IEspTpTargetCluster>& clusterList);
    void getTargetClusterList(IArrayOf<IEspTpLogicalCluster>& clusters, const char* clusterType = NULL, const char* clusterName = NULL);
    void queryTargetClusterProcess(double version, const char* processName, const char* clusterType, IArrayOf<IConstTpCluster>& list);

    IPropertyTree* getEnvironment(const char* xpath);
};


class CCluster
{
public:
    CCluster(const char* cluster): conn(querySDS().connect(StringBuffer("/Status/Servers/Server[@thorname=\"").append(cluster).append("\"]").str(),myProcessSession(),RTM_SUB,SDS_LOCK_TIMEOUT))
    {
        if (!conn)
            throw MakeStringException(0,"Cannot connect to SDS cluster %s",cluster);
    }

    operator IRemoteConnection* () { return conn.get(); }
    IRemoteConnection* operator->() { return conn.get(); }

private:
    Owned<IRemoteConnection> conn;
};

class TPWRAPPER_API CClusterQueue // looks up all thor clusters active based on queue name
{
public:
    CClusterQueue(const char* qname);
    IArrayOf<IPropertyTree> thors;
};

#endif //_ESPWIZ_TpWrapper_HPP__

