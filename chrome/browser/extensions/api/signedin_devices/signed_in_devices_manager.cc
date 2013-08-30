// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/signedin_devices/signed_in_devices_manager.h"

#include <string>

#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/api/signedin_devices/signedin_devices_api.h"
#include "chrome/browser/extensions/event_router.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/glue/device_info.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/extensions/api/signedin_devices.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"

using browser_sync::DeviceInfo;
namespace extensions {

SignedInDevicesChangeObserver::SignedInDevicesChangeObserver(
    const std::string& extension_id,
    Profile* profile) : extension_id_(extension_id),
                        profile_(profile) {
  ProfileSyncService* pss = ProfileSyncServiceFactory::GetForProfile(profile_);
  if (pss) {
    pss->AddObserverForDeviceInfoChange(this);
  }
}

SignedInDevicesChangeObserver::~SignedInDevicesChangeObserver() {
  ProfileSyncService* pss = ProfileSyncServiceFactory::GetForProfile(profile_);
  if (pss) {
    pss->RemoveObserverForDeviceInfoChange(this);
  }

}

void SignedInDevicesChangeObserver::OnDeviceInfoChange() {
  // There is a change in the list of devices. Get all devices and send them to
  // the listener.
  ScopedVector<DeviceInfo> devices = GetAllSignedinDevices(extension_id_,
                                                           profile_);

  scoped_ptr<base::ListValue> result(new base::ListValue());

  for (ScopedVector<DeviceInfo>::const_iterator it = devices.begin();
       it != devices.end();
       ++it) {
    result->Append((*it)->ToValue());
  }

  scoped_ptr<Event> event(new Event(
      api::signedin_devices::OnDeviceInfoChange::kEventName,
      result.Pass()));

  event->restrict_to_profile = profile_;

  ExtensionSystem::Get(profile_)->event_router()->DispatchEventToExtension(
      extension_id_, event.Pass());
}

static base::LazyInstance<ProfileKeyedAPIFactory<SignedInDevicesManager> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<SignedInDevicesManager>*
    SignedInDevicesManager::GetFactoryInstance() {
  return &g_factory.Get();
}

SignedInDevicesManager::SignedInDevicesManager()
    : profile_(NULL) {}

SignedInDevicesManager::SignedInDevicesManager(Profile* profile)
    : profile_(profile) {
  extensions::EventRouter* router = extensions::ExtensionSystem::Get(
      profile_)->event_router();

  if (router) {
    router->RegisterObserver(
        this, api::signedin_devices::OnDeviceInfoChange::kEventName);
  }

  // Register for unload event so we could clear all our listeners when
  // extensions have unloaded.
  registrar_.Add(this, chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_->GetOriginalProfile()));
}

SignedInDevicesManager::~SignedInDevicesManager() {}

void SignedInDevicesManager::OnListenerAdded(
    const EventListenerInfo& details) {
  for (ScopedVector<SignedInDevicesChangeObserver>::const_iterator it =
           change_observers_.begin();
           it != change_observers_.end();
           ++it) {
    if ((*it)->extension_id() == details.extension_id) {
      DCHECK(false) <<"OnListenerAded fired twice for same extension";
      return;
    }
  }

  change_observers_.push_back(new SignedInDevicesChangeObserver(
      details.extension_id,
      profile_));
}

void SignedInDevicesManager::OnListenerRemoved(
    const EventListenerInfo& details) {
  RemoveChangeObserverForExtension(details.extension_id);
}


void SignedInDevicesManager::RemoveChangeObserverForExtension(
    const std::string& extension_id) {
  for (ScopedVector<SignedInDevicesChangeObserver>::iterator it =
           change_observers_.begin();
           it != change_observers_.end();
           ++it) {
    if ((*it)->extension_id() == extension_id) {
      change_observers_.erase(it);
      return;
    }
  }
}

void SignedInDevicesManager::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(type, chrome::NOTIFICATION_EXTENSION_UNLOADED);
  UnloadedExtensionInfo* reason =
      content::Details<UnloadedExtensionInfo>(details).ptr();
  RemoveChangeObserverForExtension(reason->extension->id());
}

}  // namespace extensions
