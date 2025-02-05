#pragma once

#include "maintenance_target.h"

#include <yt/yt/server/master/cypress_server/node_detail.h>

namespace NYT::NMaintenanceTrackerServer {

////////////////////////////////////////////////////////////////////////////////

class TClusterProxyNode
    : public NCypressServer::TMapNode
    , public TMaintenanceTarget<TClusterProxyNode, EMaintenanceType::Ban>
{
public:
    using TMapNode::TMapNode;

    void Save(NCellMaster::TSaveContext& context) const override;
    void Load(NCellMaster::TLoadContext& context) override;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NMaintenanceTrackerServer
