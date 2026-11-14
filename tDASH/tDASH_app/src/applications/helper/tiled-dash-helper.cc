/*
 * Copyright (c) 2008 INRIA
 * Copyright (c) 2013 Magister Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Original work author (from packet-sink-helper.cc):
 * - Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *
 * Converted to 3GPP HTTP web browsing traffic models by:
 * - Budiarto Herman <budiarto.herman@magister.fi>
 *
 */

#include "tiled-dash-helper.h"
#include "ns3/tiled-dash-server.h" //dio koji je falio u helperima
#include "ns3/tiled-dash-client.h"
#include <ns3/names.h>

namespace ns3
{

// Tiled DASH HELPER /////////////////////////////////////////////////////////
TiledDashClientHelper::TiledDashClientHelper()
{
    m_factory.SetTypeId("ns3::TiledDashClient");
    //m_factory.Set("ServerAddress", AddressValue(address));
}
TiledDashClientHelper::TiledDashClientHelper(const Address& address)
{
    m_factory.SetTypeId("ns3::TiledDashClient");
    m_factory.Set("ServerAddress", AddressValue(address));
}

void
TiledDashClientHelper::SetAttribute(const std::string& name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
TiledDashClientHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
TiledDashClientHelper::Install(const std::string& nodeName) const
{
    Ptr<Node> node = Names::Find<Node>(nodeName);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
TiledDashClientHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }

    return apps;
}

Ptr<Application>
TiledDashClientHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<Application>();
    node->AddApplication(app);
    return app;
}

// HTTP SERVER HELPER /////////////////////////////////////////////////////////

TiledDashServerHelper::TiledDashServerHelper()
{
    m_factory.SetTypeId("ns3::TiledDashServer");
}

void
TiledDashServerHelper::SetAttribute(const std::string& name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
TiledDashServerHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
TiledDashServerHelper::Install(const std::string& nodeName) const
{
    Ptr<Node> node = Names::Find<Node>(nodeName);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
TiledDashServerHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }

    return apps;
}

Ptr<Application>
TiledDashServerHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<Application>();
    node->AddApplication(app);

    return app;
}

} // namespace ns3
