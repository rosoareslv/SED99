/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "Screenshot.h"

#include "system_gl.h"
#include <vector>

#include "ServiceBroker.h"
#include "Util.h"
#include "URL.h"

#include "pictures/Picture.h"

#ifdef TARGET_RASPBERRY_PI
#include "platform/linux/RBP.h"
#endif

#include "filesystem/File.h"
#include "guilib/GUIComponent.h"
#include "windowing/GraphicContext.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"

#include "utils/JobManager.h"
#include "utils/URIUtils.h"
#include "utils/log.h"
#include "settings/SettingPath.h"
#include "settings/Settings.h"
#include "settings/windows/GUIControlSettings.h"

#if defined(HAS_LIBAMCODEC)
#include "utils/ScreenshotAML.h"
#endif

#if defined(TARGET_WINDOWS)
#include "rendering/dx/DeviceResources.h"
#include <wrl/client.h>
using namespace Microsoft::WRL;
#endif

using namespace XFILE;

CScreenshotSurface::CScreenshotSurface()
{
  m_width = 0;
  m_height = 0;
  m_stride = 0;
  m_buffer = NULL;
}

CScreenshotSurface::~CScreenshotSurface()
{
  delete m_buffer;
}

bool CScreenshotSurface::capture()
{
#if defined(TARGET_RASPBERRY_PI)
  g_RBP.GetDisplaySize(m_width, m_height);
  m_buffer = g_RBP.CaptureDisplay(m_width, m_height, &m_stride, true, false);
  if (!m_buffer)
    return false;
#elif defined(TARGET_WINDOWS)

  CSingleLock lock(CServiceBroker::GetWinSystem()->GetGfxContext());

  CServiceBroker::GetGUI()->GetWindowManager().Render();

  auto deviceResources = DX::DeviceResources::Get();
  deviceResources->FinishCommandList();

  ComPtr<ID3D11DeviceContext> pImdContext = deviceResources->GetImmediateContext();
  ComPtr<ID3D11DeviceContext> pContext = deviceResources->GetD3DContext();
  ComPtr<ID3D11Device> pDevice = deviceResources->GetD3DDevice();

  ComPtr<ID3D11RenderTargetView> pRTView = nullptr;
  pContext->OMGetRenderTargets(1, pRTView.GetAddressOf(), nullptr);
  if (pRTView == nullptr)
    return false;

  ComPtr<ID3D11Resource> pRTResource = nullptr;
  pRTView->GetResource(pRTResource.GetAddressOf());

  ComPtr<ID3D11Texture2D> pCopyTexture = nullptr;
  ComPtr<ID3D11Texture2D> pRTTexture = nullptr;
  if (FAILED(pRTResource.As(&pRTTexture)))
    return false;

  D3D11_TEXTURE2D_DESC desc = { 0 };
  pRTTexture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;

  if (SUCCEEDED(pDevice->CreateTexture2D(&desc, nullptr, pCopyTexture.GetAddressOf())))
  {
    // take copy
    pImdContext->CopyResource(pCopyTexture.Get(), pRTTexture.Get());

    D3D11_MAPPED_SUBRESOURCE res;
    if (SUCCEEDED(pImdContext->Map(pCopyTexture.Get(), 0, D3D11_MAP_READ, 0, &res)))
    {
      m_width = desc.Width;
      m_height = desc.Height;
      m_stride = res.RowPitch;
      m_buffer = new unsigned char[m_height * m_stride];
      memcpy(m_buffer, res.pData, m_height * m_stride);
      pImdContext->Unmap(pCopyTexture.Get(), 0);
    }
    else
      CLog::LogFunction(LOGERROR, __FUNCTION__, "MAP_READ failed.");
  }
#elif defined(HAS_GL) || defined(HAS_GLES)

  CSingleLock lock(CServiceBroker::GetWinSystem()->GetGfxContext());
  CServiceBroker::GetGUI()->GetWindowManager().Render();
#ifndef HAS_GLES
  glReadBuffer(GL_BACK);
#endif
  //get current viewport
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  m_width  = viewport[2] - viewport[0];
  m_height = viewport[3] - viewport[1];
  m_stride = m_width * 4;
  unsigned char* surface = new unsigned char[m_stride * m_height];

  //read pixels from the backbuffer
#if HAS_GLES >= 2
  glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)surface);
#else
  glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_BGRA, GL_UNSIGNED_BYTE, (GLvoid*)surface);
#endif

  //make a new buffer and copy the read image to it with the Y axis inverted
  m_buffer = new unsigned char[m_stride * m_height];
  for (int y = 0; y < m_height; y++)
  {
#ifdef HAS_GLES
    // we need to save in BGRA order so XOR Swap RGBA -> BGRA
    unsigned char* swap_pixels = surface + (m_height - y - 1) * m_stride;
    for (int x = 0; x < m_width; x++, swap_pixels+=4)
    {
      std::swap(swap_pixels[0], swap_pixels[2]);
    }
#endif
    memcpy(m_buffer + y * m_stride, surface + (m_height - y - 1) *m_stride, m_stride);
  }

  delete [] surface;

#if defined(HAS_LIBAMCODEC)
  // Captures the current visible videobuffer and blend it into m_buffer (captured overlay)
  CScreenshotAML::CaptureVideoFrame(m_buffer, m_width, m_height);
#endif

#else
  //nothing to take a screenshot from
  return false;
#endif

  return true;
}

void CScreenShot::TakeScreenshot(const std::string &filename, bool sync)
{

  CScreenshotSurface surface;
  if (!surface.capture())
  {
    CLog::Log(LOGERROR, "Screenshot %s failed", CURL::GetRedacted(filename).c_str());
    return;
  }

  CLog::Log(LOGDEBUG, "Saving screenshot %s", CURL::GetRedacted(filename).c_str());

  //set alpha byte to 0xFF
  for (int y = 0; y < surface.m_height; y++)
  {
    unsigned char* alphaptr = surface.m_buffer - 1 + y * surface.m_stride;
    for (int x = 0; x < surface.m_width; x++)
      *(alphaptr += 4) = 0xFF;
  }

  //if sync is true, the png file needs to be completely written when this function returns
  if (sync)
  {
    if (!CPicture::CreateThumbnailFromSurface(surface.m_buffer, surface.m_width, surface.m_height, surface.m_stride, filename))
      CLog::Log(LOGERROR, "Unable to write screenshot %s", CURL::GetRedacted(filename).c_str());

    delete [] surface.m_buffer;
    surface.m_buffer = NULL;
  }
  else
  {
    //make sure the file exists to avoid concurrency issues
    FILE* fp = fopen(filename.c_str(), "w");
    if (fp)
      fclose(fp);
    else
      CLog::Log(LOGERROR, "Unable to create file %s", CURL::GetRedacted(filename).c_str());

    //write .png file asynchronous with CThumbnailWriter, prevents stalling of the render thread
    //buffer is deleted from CThumbnailWriter
    CThumbnailWriter* thumbnailwriter = new CThumbnailWriter(surface.m_buffer, surface.m_width, surface.m_height, surface.m_stride, filename);
    CJobManager::GetInstance().AddJob(thumbnailwriter, NULL);
    surface.m_buffer = NULL;
  }
}

void CScreenShot::TakeScreenshot()
{
  static bool savingScreenshots = false;
  static std::vector<std::string> screenShots;
  bool promptUser = false;
  std::string strDir;

  // check to see if we have a screenshot folder yet
  std::shared_ptr<CSettingPath> screenshotSetting = std::static_pointer_cast<CSettingPath>(CServiceBroker::GetSettings().GetSetting(CSettings::SETTING_DEBUG_SCREENSHOTPATH));
  if (screenshotSetting != NULL)
  {
    strDir = screenshotSetting->GetValue();
    if (strDir.empty())
    {
      if (CGUIControlButtonSetting::GetPath(screenshotSetting, &g_localizeStrings))
        strDir = screenshotSetting->GetValue();
    }
  }

  if (strDir.empty())
  {
    strDir = "special://temp/";
    if (!savingScreenshots)
    {
      promptUser = true;
      savingScreenshots = true;
      screenShots.clear();
    }
  }
  URIUtils::RemoveSlashAtEnd(strDir);

  if (!strDir.empty())
  {
    std::string file = CUtil::GetNextFilename(URIUtils::AddFileToFolder(strDir, "screenshot%03d.png"), 999);

    if (!file.empty())
    {
      TakeScreenshot(file, false);
      if (savingScreenshots)
        screenShots.push_back(file);
      if (promptUser)
      { // grab the real directory
        std::string newDir;
        if (screenshotSetting != NULL)
        {
          newDir = screenshotSetting->GetValue();
          if (newDir.empty())
          {
            if (CGUIControlButtonSetting::GetPath(screenshotSetting, &g_localizeStrings))
              newDir = screenshotSetting->GetValue();
          }
        }

        if (!newDir.empty())
        {
          for (unsigned int i = 0; i < screenShots.size(); i++)
          {
            std::string file = CUtil::GetNextFilename(URIUtils::AddFileToFolder(newDir, "screenshot%03d.png"), 999);
            CFile::Copy(screenShots[i], file);
          }
          screenShots.clear();
        }
        savingScreenshots = false;
      }
    }
    else
    {
      CLog::Log(LOGWARNING, "Too many screen shots or invalid folder");
    }
  }
}
