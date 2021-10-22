/*
 * FeatureManager.cpp - implementation of the FeatureManager class
 *
 * Copyright (c) 2017-2021 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "FeatureManager.h"
#include "FeatureMessage.h"
#include "FeatureWorkerManager.h"
#include "PluginInterface.h"
#include "PluginManager.h"
#include "VeyonConfiguration.h"
#include "VeyonServerInterface.h"

Q_DECLARE_METATYPE(Feature)
Q_DECLARE_METATYPE(FeatureMessage)

// clazy:excludeall=reserve-candidates

FeatureManager::FeatureManager( QObject* parent ) :
	QObject( parent ),
	m_features(),
	m_emptyFeatureList(),
	m_pluginObjects(),
	m_dummyFeature()
{
	qRegisterMetaType<Feature>();
	qRegisterMetaType<FeatureMessage>();

	for( const auto& pluginObject : qAsConst( VeyonCore::pluginManager().pluginObjects() ) )
	{
		auto featurePluginInterface = qobject_cast<FeatureProviderInterface *>( pluginObject );

		if( featurePluginInterface )
		{
			m_pluginObjects += pluginObject;
			m_featurePluginInterfaces += featurePluginInterface;

			m_features += featurePluginInterface->featureList();
		}
	}

}



const FeatureList& FeatureManager::features( Plugin::Uid pluginUid ) const
{
	for( auto pluginObject : m_pluginObjects )
	{
		auto pluginInterface = qobject_cast<PluginInterface *>( pluginObject );
		auto featurePluginInterface = qobject_cast<FeatureProviderInterface *>( pluginObject );

		if( pluginInterface && featurePluginInterface && pluginInterface->uid() == pluginUid )
		{
			return featurePluginInterface->featureList();
		}
	}

	return m_emptyFeatureList;
}



const Feature& FeatureManager::feature( Feature::Uid featureUid ) const
{
	for( const auto& featureInterface : m_featurePluginInterfaces )
	{
		for( const auto& feature : featureInterface->featureList() )
		{
			if( feature.uid() == featureUid )
			{
				return feature;
			}
		}
	}

	return m_dummyFeature;
}



const FeatureList& FeatureManager::relatedFeatures( Feature::Uid featureUid ) const
{
	return features( pluginUid( featureUid ) );
}



Feature::Uid FeatureManager::metaFeatureUid( Feature::Uid featureUid ) const
{
	for( const auto& featureInterface : m_featurePluginInterfaces )
	{
		for( const auto& feature : featureInterface->featureList() )
		{
			if( feature.uid() == featureUid )
			{
				return featureInterface->metaFeature( featureUid );
			}
		}
	}

	return {};
}



Plugin::Uid FeatureManager::pluginUid( Feature::Uid featureUid ) const
{
	for( auto pluginObject : m_pluginObjects )
	{
		auto pluginInterface = qobject_cast<PluginInterface *>( pluginObject );
		auto featurePluginInterface = qobject_cast<FeatureProviderInterface *>( pluginObject );

		if( pluginInterface && featurePluginInterface &&
			std::find_if( featurePluginInterface->featureList().begin(),
						  featurePluginInterface->featureList().end(),
						  [&featureUid]( const Feature& feature ) { return feature.uid() == featureUid; } )
				!= featurePluginInterface->featureList().end() )
		{
			return pluginInterface->uid();
		}
	}

	return {};
}



void FeatureManager::controlFeature( Feature::Uid featureUid,
									FeatureProviderInterface::Operation operation,
									const QVariantMap& arguments,
									const ComputerControlInterfaceList& computerControlInterfaces ) const
{
	for( auto featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		featureInterface->controlFeature( featureUid, operation, arguments, computerControlInterfaces );
	}

	updateActiveFeatures( computerControlInterfaces );
}



void FeatureManager::startFeature( VeyonMasterInterface& master,
								   const Feature& feature,
								   const ComputerControlInterfaceList& computerControlInterfaces ) const
{
	vDebug() << feature.name() << computerControlInterfaces;

	for( auto featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		featureInterface->startFeature( master, feature, computerControlInterfaces );
	}

	if( feature.testFlag( Feature::Flag::Mode ) )
	{
		for( const auto& controlInterface : computerControlInterfaces )
		{
			controlInterface->setDesignatedModeFeature( feature.uid() );
		}
	}

	updateActiveFeatures( computerControlInterfaces );
}



void FeatureManager::stopFeature( VeyonMasterInterface& master,
								  const Feature& feature,
								  const ComputerControlInterfaceList& computerControlInterfaces ) const
{
	vDebug() << feature.name() << computerControlInterfaces;

	for( const auto& featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		featureInterface->stopFeature( master, feature, computerControlInterfaces );
	}

	for( const auto& controlInterface : computerControlInterfaces )
	{
		if( controlInterface->designatedModeFeature() == feature.uid() )
		{
			controlInterface->setDesignatedModeFeature( Feature::Uid() );
		}
	}

	updateActiveFeatures( computerControlInterfaces );
}



void FeatureManager::updateActiveFeatures( const ComputerControlInterfaceList& computerControlInterfaces ) const
{
	for( const auto& controlInterface : computerControlInterfaces )
	{
		controlInterface->updateActiveFeatures();
	}
}



bool FeatureManager::handleFeatureMessage( ComputerControlInterface::Pointer computerControlInterface,
										  const FeatureMessage& message ) const
{
	vDebug() << feature(message.featureUid()).name().toUtf8().constData() << message << computerControlInterface;

	bool handled = false;

	for( const auto& featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		if( featureInterface->handleFeatureMessage( computerControlInterface, message ) )
		{
			handled = true;
		}
	}

	return handled;
}



bool FeatureManager::handleFeatureMessage( VeyonServerInterface& server,
										   const MessageContext& messageContext,
										   const FeatureMessage& message ) const
{
	vDebug() << feature(message.featureUid()).name().toUtf8().constData() << message;

	if( VeyonCore::config().disabledFeatures().contains( message.featureUid().toString() ) )
	{
		vWarning() << "ignoring message as feature" << message.featureUid() << "is disabled by configuration!";
		return false;
	}

	bool handled = false;

	for( const auto& featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		if( featureInterface->handleFeatureMessage( server, messageContext, message ) )
		{
			handled = true;
		}
	}

	return handled;
}



bool FeatureManager::handleFeatureMessage( VeyonWorkerInterface& worker, const FeatureMessage& message ) const
{
	vDebug() << feature(message.featureUid()).name().toUtf8().constData() << message;

	bool handled = false;

	for( const auto& featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		if( featureInterface->handleFeatureMessage( worker, message ) )
		{
			handled = true;
		}
	}

	return handled;
}



FeatureUidList FeatureManager::activeFeatures( VeyonServerInterface& server ) const
{
	FeatureUidList features;

	for( const auto& featureInterface : qAsConst( m_featurePluginInterfaces ) )
	{
		for( const auto& feature : featureInterface->featureList() )
		{
			if( featureInterface->isFeatureActive( server, feature.uid() ) ||
				server.featureWorkerManager().isWorkerRunning( feature.uid() ) )
			{
				features.append( feature.uid() );
			}
		}
	}

	return features;
}
