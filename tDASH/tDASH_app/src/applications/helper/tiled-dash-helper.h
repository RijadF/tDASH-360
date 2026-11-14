/*
 * Copyright (c) 2025 UCC
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
 * Original work author (from 3gpp-http-helper.cc):
 *   - Budiarto Herman <budiarto.herman@magister.fi>
 *
 * Converted to tiled dash application by:
 *  - Ahmed Zahran <ahmed.zahran@ucc.ie>
 *  - Rijad Fethagic<rfetahgic1@.etf.unsa.ba>
 *  - Darijo Raca<draca@etf.unsa.ba>
 */

#ifndef TILED_DASH_HELPER_H
#define TILED_DASH_HELPER_H

#include <ns3/application-container.h>
#include <ns3/node-container.h>
#include <ns3/object-factory.h>

namespace ns3
{

/**
 * \ingroup applications
 * Helper to make it easier to instantiate an TiledDashClient on a set of nodes.
 */
class TiledDashClientHelper
{
  public:
    /**
     * Create a TiledDashClientHelper to make it easier to work with TiledDashClient
     * applications.
     * \param address The address of the remote server node to send traffic to.
     */
    TiledDashClientHelper(const Address& address);
    TiledDashClientHelper();
    /**
     * Helper function used to set the underlying application attributes, but
     * *not* the socket attributes.
     * \param name The name of the application attribute to set.
     * \param value The value of the application attribute to set.
     */
    void SetAttribute(const std::string& name, const AttributeValue& value);

    /**
     * Install a TiledDashClient on each node of the input container configured with
     * all the attributes set with SetAttribute().
     * \param c NodeContainer of the set of nodes on which an TiledDashClient
     *          will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(NodeContainer c) const;

    /**
     * Install a TiledDashClient on each node of the input container
     * configured with all the attributes set with SetAttribute().
     * \param node The node on which an TiledDashClient will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * Install a TiledDashClient on each node of the input container
     * configured with all the attributes set with SetAttribute().
     * \param nodeName The name of the node on which an TiledDashClient
     *                 will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(const std::string& nodeName) const;

  private:
    /**
     * \internal
     * Install a TiledDashClient on the node configured with all the
     * attributes set with SetAttribute().
     * \param node The node on which an TiledDashClient will be installed.
     * \return Ptr to the application installed.
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;

    /// Used to instantiate an TiledDashClient instance.
    ObjectFactory m_factory;

}; // end of `class TiledDashClientHelper`

/**
 * \ingroup http
 * Helper to make it easier to instantiate an TiledDashServer on a set of nodes.
 */
class TiledDashServerHelper
{
  public:
    /**
     * Create a TiledDashServerHelper to make it easier to work with
     * TiledDashServer applications.
     * \param address The address of the server.  [Zahran: DELETE - not needed]
     */
    //TiledDashServerHelper(const Address& address);
    TiledDashServerHelper();

    /**
     * Helper function used to set the underlying application attributes, but
     * *not* the socket attributes.
     * \param name The name of the application attribute to set.
     * \param value The value of the application attribute to set.
     */
    void SetAttribute(const std::string& name, const AttributeValue& value);

    /**
     * Install an TiledDashServer on each node of the input container
     * configured with all the attributes set with SetAttribute().
     * \param c NodeContainer of the set of nodes on which an TiledDashServer
     *              will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(NodeContainer c) const;

    /**
     * Install an TiledDashServer on each node of the input container
     * configured with all the attributes set with SetAttribute().
     * \param node The node on which an TiledDashServer will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(Ptr<Node> node) const;

    /**
     * Install an TiledDashServer on each node of the input container
     * configured with all the attributes set with SetAttribute().
     * \param nodeName The name of the node on which an TiledDashServer
     *                             will be installed.
     * \return Container of Ptr to the applications installed.
     */
    ApplicationContainer Install(const std::string& nodeName) const;

  private:
    /**
     * \internal
     * Install an TiledDashServer on the node configured with all the
     * attributes set with SetAttribute().
     * \param node The node on which an TiledDashServer will be installed.
     * \return Ptr to the application installed.
     */
    Ptr<Application> InstallPriv(Ptr<Node> node) const;

    /// Used to instantiate a TiledDashServer instance.
    ObjectFactory m_factory;

}; // end of `class TiledDashServerHelper`

} // namespace ns3

#endif /* TILED_DASH_HELPER_H */
