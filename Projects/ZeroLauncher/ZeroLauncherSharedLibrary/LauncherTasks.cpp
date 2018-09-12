///////////////////////////////////////////////////////////////////////////////
///
/// Authors: Josh Davis
/// Copyright 2015, DigiPen Institute of Technology
///
///////////////////////////////////////////////////////////////////////////////
#include "Precompiled.hpp"

namespace Zero
{

//-------------------------------------------------------------------GetVersionListingTask
GetVersionListingTaskJob::GetVersionListingTaskJob(StringParam url) : DownloadTaskJob(url)
{
}

void GetVersionListingTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}
  
void GetVersionListingTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode == WebResponseCode::OK)
  {
    mState = BackgroundTaskState::Completed;
  }
  else
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
  }
}

void GetVersionListingTaskJob::PopulatePackageList()
{
  Status status;

  // Create a cog from the received data
  DataTreeLoader loader;
  loader.OpenBuffer(status, GetData());
  Cog* rootCog = Z::gFactory->CreateFromStream(Z::gEngine->GetEngineSpace(), loader, 0, nullptr);
  ReturnIf(rootCog == nullptr, , "Invalid root cog created from server list of builds");
  
  // Walk all children and add them to our list of available build packages
  HierarchyList::range children = rootCog->GetChildren();
  for(; !children.Empty(); children.PopFront())
  {
    mPackages.PushBack(&children.Front());
  }
}

//-------------------------------------------------------------------GetDataTask
DownloadImageTaskJob::DownloadImageTaskJob(StringParam url) : DownloadTaskJob(url)
{
  mImageWasInvalid = false;
}

void DownloadImageTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadImageTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode == WebResponseCode::OK)
  {
    //just save the data
    Status status;
    LoadImage(status, (byte*)event->mData.Data(), event->mData.SizeInBytes(), &mImage);

    if(status.Failed())
    {
      Failed();
      mImageWasInvalid = true;
      return;
    }
  }
  else
  {
    DoNotifyWarning("Failed Download", "Failed to download image");
    Failed();
  }

  mState = BackgroundTaskState::Completed;
  UpdateDownloadProgress();
}

//-------------------------------------------------------------------LoadImageFromDiskTask
LoadImageFromDiskTaskJob::LoadImageFromDiskTaskJob(StringParam path)
  : DownloadImageTaskJob(path)
{
  mPath = path;
}

void LoadImageFromDiskTaskJob::Execute()
{
  if(FileExists(mPath) == false)
  {
    mState = BackgroundTaskState::Failed;
    return;
  }

  Status status;
  LoadImage(status, mPath, &mImage);

  if(status.Failed())
  {
    mState = BackgroundTaskState::Failed;
    return;
  }

  mState = BackgroundTaskState::Completed;
  UpdateProgress(GetName(), 1.0f);
}

//-------------------------------------------------------------------GetDataTask
GetDataTaskJob::GetDataTaskJob(StringParam url) : DownloadTaskJob(url)
{

}

void GetDataTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void GetDataTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode == WebResponseCode::OK)
  {
    //just save the data
    mData = event->mData;
  }
  else
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
  }

  mState = BackgroundTaskState::Completed;
  UpdateDownloadProgress();
}

//-------------------------------------------------------------------DownloadStandaloneTask
DownloadStandaloneTaskJob::DownloadStandaloneTaskJob(StringParam url) : DownloadTaskJob(url)
{
}

void DownloadStandaloneTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadStandaloneTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode == WebResponseCode::OK)
  {
    String data = event->mData;

    // Make sure the directory where we're extract to exists
    CreateDirectoryAndParents(mInstallLocation);

    // Validate that we got any data (otherwise archive fails)
    if(data.SizeInBytes() == 0)
    {
      String msg = String::Format("Download of build from url '%s' failed", mRequest->mUrl.c_str());
      DoNotifyWarning("Download failed", msg);
      return;
    }

    // Save the meta file out for this build (cached as a string to avoid threading issues)
    String metaFilePath = ZeroBuild::GetMetaFilePath(mInstallLocation);
    WriteStringRangeToFile(metaFilePath, mMetaContents);

    // Decompress the archive to our install location
    Archive archive(ArchiveMode::Decompressing);
    ByteBufferBlock buffer((byte*)data.Data(), data.SizeInBytes(), false);
    // Unfortunately, archive doesn't return any failed state so we could get back an invalid zip.
    // Currently this should only happen if the server code is wrong.
    archive.ReadBuffer(ArchiveReadFlags::All, buffer);
    archive.ExportToDirectory(ArchiveExportMode::Overwrite, mInstallLocation);

    mState = BackgroundTaskState::Completed;
  }
  else
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
  }

  mRequest->ClearAll();
}

//-------------------------------------------------------------------InstallBuildTask
InstallBuildTaskJob::InstallBuildTaskJob()
{

}

InstallBuildTaskJob::InstallBuildTaskJob(StringParam data)
{
  mData = data;
}

void InstallBuildTaskJob::LoadFromFile(StringParam filePath)
{
  //build the task to start the install
  size_t fileSize;
  byte* data = ReadFileIntoMemory(filePath.c_str(), fileSize, 1);
  data[fileSize] = 0;
  mData = String((char*)data, fileSize);
}

void InstallBuildTaskJob::Execute()
{
  InstallBuild();

  mState = BackgroundTaskState::Completed;
  UpdateProgress(GetName(), 1.0f);
}

void InstallBuildTaskJob::InstallBuild()
{
  //if the build folder already exists for some reason then replace it (maybe prompt later)
  if(FileExists(mInstallLocation))
    DeleteDirectory(mInstallLocation);

  //make sure the directory where we're extract to exists
  CreateDirectoryAndParents(mInstallLocation);

  // Save the meta file out for this build (cached as a string to avoid threading issues)
  String metaFilePath = ZeroBuild::GetMetaFilePath(mInstallLocation);
  WriteStringRangeToFile(metaFilePath, mMetaContents);

  //decompress the archive to our install location
  Archive archive(ArchiveMode::Decompressing);
  ByteBufferBlock buffer((byte*)mData.Data(), mData.SizeInBytes(), false);
  archive.ReadBuffer(ArchiveReadFlags::All, buffer);
  archive.ExportToDirectory(ArchiveExportMode::Overwrite, mInstallLocation);

  mState = BackgroundTaskState::Completed;
}

//-------------------------------------------------------------------DeleteDirectoryJob
DeleteDirectoryJob::DeleteDirectoryJob(StringParam directory, StringParam rootDirectory, bool recursivelyDeleteEmpty)
{
  mDirectory = directory;
  mRootDirectory = rootDirectory;
  mRecursivelyDeleteEmpty = recursivelyDeleteEmpty;
}

void DeleteDirectoryJob::Execute()
{
  if(DirectoryExists(mDirectory) == false)
    return;

  // Try to preserve the user's marked bad data if it existed
  String markedBadPath = FilePath::CombineWithExtension(mDirectory, "VersionMarkedBad", ".txt");
  String markedBadData;
  if(FileExists(markedBadPath))
    markedBadData = ReadFileIntoString(markedBadPath);

  // Remove the directory recursively
  DeleteDirectory(mDirectory);

  // Recursively delete empty parent folders until we reach the root directory
  if(mRecursivelyDeleteEmpty)
  {
    String parentDir = FilePath::GetDirectoryPath(mDirectory);
    while(!parentDir.Empty() && mRootDirectory != parentDir && FileRange(parentDir).Empty())
    {
      DeleteDirectory(parentDir);
      parentDir = FilePath::GetDirectoryPath(parentDir);
    }
  }

  // If it did exist then write the file back out
  if(!markedBadData.Empty())
  {
    CreateDirectoryAndParents(mDirectory);
    WriteStringRangeToFile(markedBadPath, markedBadData);
  }

  mState = BackgroundTaskState::Completed;
  UpdateProgress("DeleteDirectory", 1.0f);
}

//-------------------------------------------------------------------GetTemplateListingTask
GetTemplateListingTaskJob::GetTemplateListingTaskJob(StringParam url) : DownloadTaskJob(url)
{

}

void GetTemplateListingTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void GetTemplateListingTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode == WebResponseCode::OK)
  {
    mState = BackgroundTaskState::Completed;
  }
  else
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
  }
}

void GetTemplateListingTaskJob::PopulateTemplateList()
{
  Status status;
  // Create a cog from the received data
  DataTreeLoader loader;
  loader.OpenBuffer(status, GetData());
  Cog* rootCog = Z::gFactory->CreateFromStream(Z::gEngine->GetEngineSpace(), loader, 0, nullptr);
  ReturnIf(rootCog == nullptr, , "Invalid root cog created from server list of builds");

  // Walk all children and add them to our list of available build packages
  HierarchyList::range children = rootCog->GetChildren();
  for(; !children.Empty(); children.PopFront())
  {
    mTemplates.PushBack(&children.Front());
  }
}

//-------------------------------------------------------------------DownloadTemplateTask
DownloadTemplateTaskJob::DownloadTemplateTaskJob(StringParam templateUrl, TemplateProject* project)
  : DownloadTaskJob(templateUrl)
{
  mTemplate = project;
}

void DownloadTemplateTaskJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadTemplateTaskJob::OnReponse(WebResponseEvent* event)
{
  if(event->mResponseCode != WebResponseCode::OK)
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
    return;
  }

  String data = event->mData;

  CreateDirectoryAndParents(mTemplateInstallLocation);

  ZeroTemplate* zeroTemplate = mTemplate->GetZeroTemplate(false);

  // Save the template meta file
  FilePath::GetFileNameWithoutExtension(mTemplateNameWithoutExtension);
  String metaFilePath = FilePath::CombineWithExtension(mTemplateInstallLocation, mTemplateNameWithoutExtension, ".meta");
  WriteToFile(metaFilePath.c_str(), (byte*)mMetaContents.c_str(), mMetaContents.SizeInBytes());

  // Save the template zip
  String templateFilePath = FilePath::CombineWithExtension(mTemplateInstallLocation, mTemplateNameWithoutExtension, TemplateProject::mExtensionWithDot);
  WriteToFile(templateFilePath.c_str(), (byte*)data.c_str(), data.SizeInBytes());

  // Save the icon image
  String iconFilePath = FilePath::Combine(mTemplateInstallLocation, zeroTemplate->mIconUrl);
  Status status;
  SaveImage(status, iconFilePath, &mTemplate->mIconImage, ImageSaveFormat::Png);
  
  mState = BackgroundTaskState::Completed;
}

//-------------------------------------------------------------------DownloadAndCreateTemplateTask
DownloadAndCreateTemplateTaskJob::DownloadAndCreateTemplateTaskJob(StringParam templateUrl, TemplateProject* project)
  : DownloadTemplateTaskJob(templateUrl, project)
{
  
}

void DownloadAndCreateTemplateTaskJob::Execute()
{
  mCachedProject = nullptr;
  // If the template is downloaded and not available on the server then just create from the local path
  if(mTemplate->mIsDownloaded && !mTemplate->mIsOnServer)
  {
    // @JoshD: This is currently broken because the even will be sent before the listener.
    UpdateProgress("CreatedTemplate", 1.0f);
    mState = BackgroundTaskState::Completed;
    return;
  }

  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadAndCreateTemplateTaskJob::OnReponse(WebResponseEvent* event)
{
  // Nothing to do here (we can only run serialization on the main thread)
}

CachedProject* DownloadAndCreateTemplateTaskJob::GetOrCreateCachedProject(ProjectCache* projectCache)
{
  // The cached project was already created, just return it
  if(mCachedProject != nullptr)
    return mCachedProject;

  if(mRequest->mResponseCode != WebResponseCode::OK)
  {
    DoNotifyWarning("Failed Download", "Failed to download newest version");
    Failed();
    return nullptr;
  }

  mState = BackgroundTaskState::Completed;

  // Save the template file to a temporary location, if it already exists then delete the old file.
  String templatePath = GetTemporaryDirectory();
  String templateFilePath = FilePath::CombineWithExtension(templatePath, mTemplateNameWithoutExtension, TemplateProject::mExtensionWithDot);
  if(FileExists(templateFilePath))
    DeleteFile(templateFilePath);
  String data = GetData();
  WriteToFile(templateFilePath.c_str(), (byte*)data.c_str(), data.SizeInBytes());

  // From the downloaded file, create the project
  CreateFromTemplateFile(templateFilePath, projectCache);

  return mCachedProject;
}

void DownloadAndCreateTemplateTaskJob::CreateFromTemplateFile(StringParam templateFilePath, ProjectCache* projectCache)
{
  // Create the project from the template
  String projectFolder = FilePath::Combine(mProjectInstallLocation, mProjectName);
  mCachedProject = projectCache->CreateProjectFromTemplate(mProjectName, projectFolder, templateFilePath, mBuildId, mProjectTags);
  
  if(mCachedProject == nullptr)
    mState = BackgroundTaskState::Failed;
  else
    mState = BackgroundTaskState::Completed;
}

//-------------------------------------------------------------------DownloadLauncherPatchInstallerJob
DownloadLauncherPatchInstallerJob::DownloadLauncherPatchInstallerJob(StringParam url, StringParam rootDownloadLocation) : DownloadTaskJob(url)
{
  mRootDownloadLocation = rootDownloadLocation;
  mIsNewPatchAvailable = false;
}

void DownloadLauncherPatchInstallerJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadLauncherPatchInstallerJob::OnReponse(WebResponseEvent* event)
{
  // Check if there's no new installer available, either from a failed request or getting no data back
  String data = event->mData;
  if(event->mResponseCode != WebResponseCode::OK || data.SizeInBytes() == 0)
  {
    mIsNewPatchAvailable = false;
    return;
  }

  // Otherwise, we have a new installer
  mIsNewPatchAvailable = true;

  //extract the archive to the download directory
  Archive archive(ArchiveMode::Decompressing);
  ByteBufferBlock buffer((byte*)data.Data(), data.SizeInBytes(), false);
  archive.ReadBuffer(ArchiveReadFlags::All, buffer);

  // Find the patch id from the archive. If we failed to find one
  // then something is wrong so report a failure.
  String patchId = FindPatchId(archive);
  if(patchId.Empty())
  {
    mState = BackgroundTaskState::Failed;
    mIsNewPatchAvailable = false;
    return;
  }

  // Extract the patch to the patch folder location
  String downloadPath = FilePath::Combine(mRootDownloadLocation, patchId);
  archive.ExportToDirectory(ArchiveExportMode::Overwrite, downloadPath);

  mState = BackgroundTaskState::Completed;
}

String DownloadLauncherPatchInstallerJob::FindPatchId(Archive& archive)
{
  // Find the version id file and return it's contents
  for(Archive::range range = archive.GetEntries(); !range.Empty(); range.PopFront())
  {
    ArchiveEntry& entry = range.Front();
    if(entry.Name != "ZeroLauncherVersionId.txt")
      continue;

    String patchId((cstr)entry.Full.Data, entry.Full.Size);
    return patchId;
  }
  // Otherwise return an empty string
  return String();
}

//-------------------------------------------------------------------CheckForLauncherMajorInstallerJob
CheckForLauncherMajorInstallerJob::CheckForLauncherMajorInstallerJob(StringParam url) : DownloadTaskJob(url)
{
  mIsNewInstallerAvailable = false;
}

void CheckForLauncherMajorInstallerJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void CheckForLauncherMajorInstallerJob::OnReponse(WebResponseEvent* event)
{
  // Check if there's no new installer available, either from a failed request or getting no data back
  String data = event->mData;
  if(event->mResponseCode != WebResponseCode::OK || data.SizeInBytes() == 0)
  {
    mIsNewInstallerAvailable = false;
    return;
  }

  // Otherwise, we have a new installer
  mIsNewInstallerAvailable = true;
}

//-------------------------------------------------------------------DownloadLauncherMajorInstallerJob
DownloadLauncherMajorInstallerJob::DownloadLauncherMajorInstallerJob(StringParam url) : DownloadTaskJob(url)
{
  mIsNewInstallerAvailable = false;
}

void DownloadLauncherMajorInstallerJob::Execute()
{
  AsyncWebRequest* request = mRequest;
  ConnectThisTo(request, Events::WebResponseComplete, OnReponse);
  DownloadTaskJob::Execute();
}

void DownloadLauncherMajorInstallerJob::OnReponse(WebResponseEvent* event)
{
  // Check if there's no new installer available, either from a failed request or getting no data back
  String data = event->mData;
  if(event->mResponseCode != WebResponseCode::OK || data.SizeInBytes() == 0)
  {
    mIsNewInstallerAvailable = false;
    return;
  }

  // Otherwise, we have a new installer
  mIsNewInstallerAvailable = true;
  // Write the installer to a temp location
  String temporaryDirectory = GetTemporaryDirectory();
  mInstallerPath = FilePath::Combine(temporaryDirectory, "ZeroLauncherInstaller.exe");
  if(FileExists(mInstallerPath))
    DeleteFile(mInstallerPath);

  WriteToFile(mInstallerPath.c_str(), (byte*)data.c_str(), data.SizeInBytes());
  mState = BackgroundTaskState::Completed;
}

//-------------------------------------------------------------------BackupProjectJob
BackupProjectJob::BackupProjectJob(StringParam projectPath, StringParam destFilePath)
{
  mOpenDirectoryOnCompletion = true;
  mProjectPath = projectPath;
  mDestinationFilePath = destFilePath;
}

void BackupProjectJob::Execute()
{
  String targetDirector = FilePath::GetDirectoryPath(mDestinationFilePath);
  CreateDirectoryAndParents(targetDirector);

  // Collect all of the files to archive
  Array<ArchiveData> files;
  GetFileList(mProjectPath, String(), files);

  // Add each file to the archive, sending out progress events every so often
  Archive projectArchive(ArchiveMode::Compressing);
  for(size_t i = 0; i < files.Size(); ++i)
  {
    ArchiveData& data = files[i];
    projectArchive.AddFile(data.mFullFilePath, data.mRelativePath);

    if(i % 5)
      UpdateProgress("ArchivingProject", i / (float)files.Size());
  }

  // Write the zip to the final file location
  projectArchive.WriteZipFile(mDestinationFilePath);

  // Mark that we've finished
  mState = BackgroundTaskState::Completed;
  UpdateProgress("ArchivingProject", 1.0f);

  // If requested, open the target directory on completion.
  if(mOpenDirectoryOnCompletion)
    Os::SystemOpenFile(targetDirector.c_str());
}

void BackupProjectJob::GetFileList(StringParam path, StringParam parentPath, Array<ArchiveData>& fileList)
{
  FileRange fileRange(path);
  for(; !fileRange.Empty(); fileRange.PopFront())
  {
    String localPath = fileRange.Front();
    String fullPath = FilePath::Combine(path, fileRange.Front());
    String relativePath = FilePath::Combine(parentPath, localPath);

    // Recurse down directories
    if(DirectoryExists(fullPath))
    {
      String subPath = FilePath::Combine(path, localPath);
      GetFileList(subPath, relativePath, fileList);
    }
    // Add files (need relative path for archive)
    else
    {
      ArchiveData& data = fileList.PushBack();
      data.mFullFilePath = fullPath;
      data.mRelativePath = relativePath;
    }
  }
}

}//namespace Zero
