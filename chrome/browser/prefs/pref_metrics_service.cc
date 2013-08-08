// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/pref_metrics_service.h"

#include "base/metrics/histogram.h"
#include "base/prefs/pref_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"

PrefMetricsService::PrefMetricsService(Profile* profile)
    : profile_(profile) {
  RecordLaunchPrefs();
}

PrefMetricsService::~PrefMetricsService() {
}

void PrefMetricsService::RecordLaunchPrefs() {
  PrefService* prefs = profile_->GetPrefs();
  bool show_home_button = prefs->GetBoolean(prefs::kShowHomeButton);
  bool home_page_is_ntp = prefs->GetBoolean(prefs::kHomePageIsNewTabPage);
  UMA_HISTOGRAM_BOOLEAN("Settings.ShowHomeButton", show_home_button);
  if (show_home_button) {
    UMA_HISTOGRAM_BOOLEAN("Settings.GivenShowHomeButton_HomePageIsNewTabPage",
        home_page_is_ntp);
  }
  int restore_on_startup = prefs->GetInteger(prefs::kRestoreOnStartup);
  UMA_HISTOGRAM_ENUMERATION("Settings.StartupPageLoadSettings",
      restore_on_startup, SessionStartupPref::kPrefValueMax);
  if (restore_on_startup == SessionStartupPref::kPrefValueURLs) {
    const int url_list_size = prefs->GetList(
        prefs::kURLsToRestoreOnStartup)->GetSize();
    UMA_HISTOGRAM_CUSTOM_COUNTS(
        "Settings.StartupPageLoadURLs", url_list_size, 1, 50, 20);
  }
}

// static
PrefMetricsService::Factory* PrefMetricsService::Factory::GetInstance() {
  return Singleton<PrefMetricsService::Factory>::get();
}

// static
PrefMetricsService* PrefMetricsService::Factory::GetForProfile(
    Profile* profile) {
  return static_cast<PrefMetricsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

PrefMetricsService::Factory::Factory()
    : BrowserContextKeyedServiceFactory(
        "PrefMetricsService",
        BrowserContextDependencyManager::GetInstance()) {
}

PrefMetricsService::Factory::~Factory() {
}

BrowserContextKeyedService*
PrefMetricsService::Factory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new PrefMetricsService(static_cast<Profile*>(profile));
}

bool PrefMetricsService::Factory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

bool PrefMetricsService::Factory::ServiceIsNULLWhileTesting() const {
  return false;
}

content::BrowserContext* PrefMetricsService::Factory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}