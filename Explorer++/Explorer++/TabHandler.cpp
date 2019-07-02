// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "Explorer++.h"
#include "Config.h"
#include "LoadSaveInterface.h"
#include "MainImages.h"
#include "MainResource.h"
#include "RenameTabDialog.h"
#include "ShellBrowser/iShellView.h"
#include "ShellBrowser/SortModes.h"
#include "TabContainer.h"
#include "../Helper/Helper.h"
#include "../Helper/iDirectoryMonitor.h"
#include "../Helper/ListViewHelper.h"
#include "../Helper/Macros.h"
#include "../Helper/MenuHelper.h"
#include "../Helper/ShellHelper.h"
#include "../Helper/TabHelper.h"
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/map.hpp>
#include <algorithm>
#include <list>

extern std::vector<std::wstring> g_commandLineDirectories;

void Explorerplusplus::InitializeTabs()
{
	/* The tab backing will hold the tab window. */
	CreateTabBacking();

	m_tabContainer = TabContainer::Create(m_hTabBacking, this, this, m_navigation, this, m_hLanguageModule, m_config);
	m_tabContainer->tabUpdatedSignal.AddObserver(boost::bind(&Explorerplusplus::OnTabUpdated, this, _1, _2));

	m_navigation->navigationCompletedSignal.AddObserver(boost::bind(&Explorerplusplus::OnNavigationCompleted, this, _1), boost::signals2::at_front);

	/* Create the toolbar that will appear on the tab control.
	Only contains the close button used to close tabs. */
	TCHAR szTabCloseTip[64];
	LoadString(m_hLanguageModule,IDS_TAB_CLOSE_TIP,szTabCloseTip,SIZEOF_ARRAY(szTabCloseTip));
	m_hTabWindowToolbar	= CreateTabToolbar(m_hTabBacking,TABTOOLBAR_CLOSE,szTabCloseTip);
}

void Explorerplusplus::OnTabUpdated(const Tab &tab, Tab::PropertyType propertyType)
{
	switch (propertyType)
	{
	case Tab::PropertyType::LOCKED:
	case Tab::PropertyType::ADDRESS_LOCKED:
		/* If the tab that was locked/unlocked is the
		currently selected tab, then the tab close
		button on the toolbar will need to be updated. */
		if (m_tabContainer->IsTabSelected(tab))
		{
			UpdateTabToolbar();
		}
		break;
	}
}

void Explorerplusplus::OnStartedBrowsing(int iTabId, const TCHAR *szFolderPath)
{
	TCHAR	szLoadingText[512];

	if (iTabId == m_tabContainer->GetSelectedTab().GetId())
	{
		TCHAR szTemp[64];
		LoadString(m_hLanguageModule, IDS_GENERAL_LOADING, szTemp, SIZEOF_ARRAY(szTemp));
		StringCchPrintf(szLoadingText, SIZEOF_ARRAY(szLoadingText), szTemp, szFolderPath);

		/* Browsing of a folder has started. Set the status bar text to indicate that
		the folder is been loaded. */
		SendMessage(m_hStatusBar, SB_SETTEXT, (WPARAM)0 | 0, (LPARAM)szLoadingText);

		/* Clear the text in all other parts of the status bar. */
		SendMessage(m_hStatusBar, SB_SETTEXT, (WPARAM)1 | 0, (LPARAM)EMPTY_STRING);
		SendMessage(m_hStatusBar, SB_SETTEXT, (WPARAM)2 | 0, (LPARAM)EMPTY_STRING);
	}
}

void Explorerplusplus::OnNavigationCompleted(const Tab &tab)
{
	if (m_tabContainer->IsTabSelected(tab))
	{
		tab.GetShellBrowser()->QueryCurrentDirectory(SIZEOF_ARRAY(m_CurrentDirectory),
			m_CurrentDirectory);
		SetCurrentDirectory(m_CurrentDirectory);

		UpdateArrangeMenuItems();

		m_nSelected = 0;

		UpdateWindowStates();
	}

	HandleDirectoryMonitoring(tab.GetId());
}

/*
* Creates a new tab. If a folder is selected,
* that folder is opened in a new tab, else
* the default directory is opened.
*/
void Explorerplusplus::OnNewTab()
{
	int		iSelected;
	HRESULT	hr;
	BOOL	bFolderSelected = FALSE;

	iSelected = ListView_GetNextItem(m_hActiveListView,
		-1, LVNI_FOCUSED | LVNI_SELECTED);

	if(iSelected != -1)
	{
		TCHAR FullItemPath[MAX_PATH];

		/* An item is selected, so get its full pathname. */
		m_pActiveShellBrowser->QueryFullItemName(iSelected, FullItemPath, SIZEOF_ARRAY(FullItemPath));

		/* If the selected item is a folder, open that folder
		in a new tab, else just use the default new tab directory. */
		if(PathIsDirectory(FullItemPath))
		{
			bFolderSelected = TRUE;
			m_tabContainer->CreateNewTab(FullItemPath, TabSettings(_selected = true));
		}
	}

	/* Either no items are selected, or the focused + selected
	item was not a folder; open the default tab directory. */
	if(!bFolderSelected)
	{
		hr = m_tabContainer->CreateNewTab(m_config->defaultTabDirectory.c_str(), TabSettings(_selected = true));

		if (FAILED(hr))
		{
			m_tabContainer->CreateNewTab(m_config->defaultTabDirectoryStatic.c_str(), TabSettings(_selected = true));
		}
	}
}

boost::signals2::connection Explorerplusplus::AddTabSelectedObserver(const TabSelectedSignal::slot_type &observer)
{
	return m_tabSelectedSignal.connect(observer);
}

HRESULT Explorerplusplus::RestoreTabs(ILoadSave *pLoadSave)
{
	TCHAR							szDirectory[MAX_PATH];
	HRESULT							hr;
	int								nTabsCreated = 0;

	if(!g_commandLineDirectories.empty())
	{
		for(const auto &strDirectory : g_commandLineDirectories)
		{
			StringCchCopy(szDirectory,SIZEOF_ARRAY(szDirectory),strDirectory.c_str());

			if(lstrcmp(strDirectory.c_str(),_T("..")) == 0)
			{
				/* Get the parent of the current directory,
				and browse to it. */
				GetCurrentDirectory(SIZEOF_ARRAY(szDirectory),szDirectory);
				PathRemoveFileSpec(szDirectory);
			}
			else if(lstrcmp(strDirectory.c_str(),_T(".")) == 0)
			{
				GetCurrentDirectory(SIZEOF_ARRAY(szDirectory),szDirectory);
			}

			hr = m_tabContainer->CreateNewTab(szDirectory, TabSettings(_selected = true));

			if(hr == S_OK)
				nTabsCreated++;
		}
	}
	else
	{
		if(m_config->startupMode == STARTUP_PREVIOUSTABS)
			nTabsCreated = pLoadSave->LoadPreviousTabs();
	}

	if(nTabsCreated == 0)
	{
		hr = m_tabContainer->CreateNewTab(m_config->defaultTabDirectory.c_str(), TabSettings(_selected = true));

		if(FAILED(hr))
			hr = m_tabContainer->CreateNewTab(m_config->defaultTabDirectoryStatic.c_str(), TabSettings(_selected = true));

		if(hr == S_OK)
		{
			nTabsCreated++;
		}
	}

	if(nTabsCreated == 0)
	{
		/* Should never end up here. */
		return E_FAIL;
	}

	/* Tabs created on startup will NOT have
	any automatic updates. The only thing that
	needs to be done is to monitor each
	directory. The tab that is finally switched
	to will have an associated update of window
	states. */
	for (auto &tab : m_tabContainer->GetAllTabs() | boost::adaptors::map_values)
	{
		HandleDirectoryMonitoring(tab.GetId());
	}

	if(!m_config->alwaysShowTabBar.get())
	{
		if(nTabsCreated == 1)
		{
			m_bShowTabBar = FALSE;
		}
	}

	/* m_iLastSelectedTab is the tab that was selected when the
	program was last closed. */
	if(m_iLastSelectedTab >= m_tabContainer->GetNumTabs() ||
		m_iLastSelectedTab < 0)
	{
		m_iLastSelectedTab = 0;
	}

	/* Set the focus back to the tab that had the focus when the program
	was last closed. */
	m_tabContainer->SelectTabAtIndex(m_iLastSelectedTab);

	return S_OK;
}

void Explorerplusplus::OnTabSelectionChanged(bool broadcastEvent)
{
	int index = TabCtrl_GetCurSel(m_tabContainer->GetHWND());

	if (index == -1)
	{
		throw std::runtime_error("No selected tab");
	}

	/* Hide the old listview. */
	ShowWindow(m_hActiveListView,SW_HIDE);

	const Tab &tab = m_tabContainer->GetTabByIndex(index);

	m_hActiveListView = tab.listView;
	m_pActiveShellBrowser = tab.GetShellBrowser();

	/* The selected tab has changed, so update the current
	directory. Although this is not needed internally, context
	menu extensions may need the current directory to be
	set correctly. */
	m_pActiveShellBrowser->QueryCurrentDirectory(SIZEOF_ARRAY(m_CurrentDirectory),
		m_CurrentDirectory);
	SetCurrentDirectory(m_CurrentDirectory);

	m_nSelected = m_pActiveShellBrowser->GetNumSelected();

	SetActiveArrangeMenuItems();
	UpdateArrangeMenuItems();

	UpdateWindowStates();

	/* Show the new listview. */
	ShowWindow(m_hActiveListView,SW_SHOW);
	SetFocus(m_hActiveListView);

	if (broadcastEvent)
	{
		m_tabSelectedSignal(tab);
	}
}

void Explorerplusplus::OnSelectTabByIndex(int iTab)
{
	int nTabs = m_tabContainer->GetNumTabs();
	int newIndex;

	if(iTab == -1)
	{
		newIndex = nTabs - 1;
	}
	else
	{
		if(iTab < nTabs)
			newIndex = iTab;
		else
			newIndex = nTabs - 1;
	}

	m_tabContainer->SelectTabAtIndex(newIndex);
}

bool Explorerplusplus::OnCloseTab()
{
	const Tab &tab = m_tabContainer->GetSelectedTab();
	return m_tabContainer->CloseTab(tab);
}

void Explorerplusplus::ShowTabBar()
{
	m_bShowTabBar = TRUE;
	UpdateLayout();
}

void Explorerplusplus::HideTabBar()
{
	m_bShowTabBar = FALSE;
	UpdateLayout();
}

HRESULT Explorerplusplus::RefreshTab(const Tab &tab)
{
	HRESULT hr = tab.GetShellBrowser()->Refresh();

	if (SUCCEEDED(hr))
	{
		OnNavigationCompleted(tab);
	}

	return hr;
}

void Explorerplusplus::UpdateTabToolbar(void)
{
	const int nTabs = m_tabContainer->GetNumTabs();

	const Tab &selectedTab = m_tabContainer->GetSelectedTab();

	if(nTabs > 1 && !(selectedTab.GetLocked() || selectedTab.GetAddressLocked()))
	{
		/* Enable the tab close button. */
		SendMessage(m_hTabWindowToolbar,TB_SETSTATE,
		TABTOOLBAR_CLOSE,TBSTATE_ENABLED);
	}
	else
	{
		/* Disable the tab close toolbar button. */
		SendMessage(m_hTabWindowToolbar,TB_SETSTATE,
		TABTOOLBAR_CLOSE,TBSTATE_INDETERMINATE);
	}
}