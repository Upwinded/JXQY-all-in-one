package com.upwinded.jxqy;

import android.Manifest;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.PackageInstaller;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;

import javax.net.ssl.HttpsURLConnection;

import org.libsdl.app.SDLActivity;

/**
 * 应用主 Activity，继承自 SDL3 的 SDLActivity。
 *
 * 提供 native 代码通过 JNI 调用的 Android 专项能力：
 *   - 查询/申请"所有文件访问权限"（MANAGE_EXTERNAL_STORAGE，Android 11+）。
 *   - 获取公开外部存储根目录（用于拼接固定外部资源目录）。
 *
 * 这些方法均为静态方法，通过 SDLActivity 的单例（mSingleton）获取当前 Activity 上下文。
 * native 侧从 SDL 当前 Activity 对象取得实际运行时类，再调用这些静态方法。
 */
public class JxqyActivity extends SDLActivity
{
	private static final int ExternalStoragePermissionRequestCode = 4301;
	private static final int ResourcePackageImportRequestCode = 4302;
	private static final int MaximumHttpsRedirects = 5;
	private static final long MaximumImportedResourcePackageBytes =
		256L * 1024L * 1024L * 1024L;
	private static final long ResourcePackageImportDiskHeadroomBytes =
		64L * 1024L * 1024L;
	private static final Object AllFilesAccessRequestLock = new Object();
	private static final Object ResourcePackageImportLock = new Object();
	private static boolean allFilesAccessRequestPending = false;
	private static boolean allFilesAccessRequestObservedDeparture = false;
	private static boolean allFilesAccessRequestCompleted = false;
	private static boolean resourcePackageImportPending = false;
	private static String resourcePackageImportResult = "";
	private static File pendingProgramInstallFile = null;

	private static boolean beginResourcePackageImport()
	{
		synchronized (ResourcePackageImportLock)
		{
			if (resourcePackageImportPending)
			{
				return false;
			}
			resourcePackageImportPending = true;
			resourcePackageImportResult = "";
			return true;
		}
	}

	private static void completeResourcePackageImport(String result)
	{
		synchronized (ResourcePackageImportLock)
		{
			resourcePackageImportPending = false;
			resourcePackageImportResult = result == null
				? "error\n无法读取所选资源包" : result;
		}
	}

	/**
	 * 消费一次资源包选择结果。空字符串表示仍在等待；其它结果以
	 * selected、cancelled 或 error 开头，由 native 侧解析。
	 */
	public static String consumeResourcePackageImportResult()
	{
		synchronized (ResourcePackageImportLock)
		{
			String result = resourcePackageImportResult;
			resourcePackageImportResult = "";
			return result;
		}
	}

	/**
	 * 使用 Android Storage Access Framework 选择一个完整资源 ZIP。
	 * 文件选择结果会先复制到应用私有缓存目录，native 不直接持有 URI。
	 */
	public static boolean requestResourcePackageImport()
	{
		if (!beginResourcePackageImport())
		{
			return false;
		}
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			completeResourcePackageImport("error\n无法打开 Android 文件选择器");
			return false;
		}
		try
		{
			activity.runOnUiThread(new Runnable()
			{
				@Override
				public void run()
				{
					try
					{
						Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
						intent.addCategory(Intent.CATEGORY_OPENABLE);
						intent.setType("application/zip");
						intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
							"application/zip",
							"application/x-zip-compressed",
							"application/octet-stream"
						});
						intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
						activity.startActivityForResult(
							intent, ResourcePackageImportRequestCode);
					}
					catch (Exception exception)
					{
						completeResourcePackageImport(
							"error\n无法打开 Android 文件选择器");
					}
				}
			});
			return true;
		}
		catch (Exception exception)
		{
			completeResourcePackageImport("error\n无法打开 Android 文件选择器");
			return false;
		}
	}

	@Override
	protected void onActivityResult(
		int requestCode,
		int resultCode,
		Intent data)
	{
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode != ResourcePackageImportRequestCode)
		{
			return;
		}
		if (resultCode != RESULT_OK || data == null || data.getData() == null)
		{
			completeResourcePackageImport("cancelled");
			return;
		}
		final Uri selectedUri = data.getData();
		final SDLActivity activity = mSingleton;
		if (activity == null)
		{
			completeResourcePackageImport("error\n无法读取所选资源包");
			return;
		}
		new Thread(new Runnable()
		{
			@Override
			public void run()
			{
				copySelectedResourcePackage(activity, selectedUri);
			}
		}, "jxqy-resource-package-import").start();
	}

	private static void copySelectedResourcePackage(
		SDLActivity activity,
		Uri selectedUri)
	{
		File temporaryFile = null;
		boolean insufficientDiskSpace = false;
		try
		{
			File importDirectory = new File(
				activity.getCacheDir(), "resource-import").getCanonicalFile();
			if ((!importDirectory.exists() && !importDirectory.mkdirs()) ||
				!importDirectory.isDirectory())
			{
				throw new IOException("cannot create import cache");
			}
			temporaryFile = new File(
				importDirectory, "selected-resource-package.zip.tmp");
			File selectedFile = new File(
				importDirectory, "selected-resource-package.zip");
			try (InputStream input = activity.getContentResolver().openInputStream(
					selectedUri);
				 OutputStream output = new FileOutputStream(temporaryFile, false))
			{
				if (input == null)
				{
					throw new IOException("selected document is unavailable");
				}
				byte[] buffer = new byte[64 * 1024];
				long copiedBytes = 0;
				int count;
				while ((count = input.read(buffer)) >= 0)
				{
					if (count == 0)
					{
						continue;
					}
					if (copiedBytes >
						MaximumImportedResourcePackageBytes - count)
					{
						throw new IOException("selected package is too large");
					}
					if (importDirectory.getUsableSpace() <
						ResourcePackageImportDiskHeadroomBytes + count)
					{
						insufficientDiskSpace = true;
						throw new IOException("insufficient import disk space");
					}
					output.write(buffer, 0, count);
					copiedBytes += count;
				}
				output.flush();
				if (copiedBytes == 0)
				{
					throw new IOException("selected package is empty");
				}
			}
			if (selectedFile.exists() && !selectedFile.delete())
			{
				throw new IOException("cannot replace import cache");
			}
			if (!temporaryFile.renameTo(selectedFile))
			{
				throw new IOException("cannot finalize import cache");
			}
			completeResourcePackageImport(
				"selected\n" + selectedFile.getAbsolutePath());
		}
		catch (Exception exception)
		{
			if (temporaryFile != null)
			{
				temporaryFile.delete();
			}
			completeResourcePackageImport(insufficientDiskSpace
				? "error\n磁盘空间不足，无法复制所选资源包"
				: "error\n无法复制所选资源包");
		}
	}

	private static boolean beginAllFilesAccessRequest()
	{
		synchronized (AllFilesAccessRequestLock)
		{
			if (allFilesAccessRequestPending)
			{
				return false;
			}
			allFilesAccessRequestPending = true;
			allFilesAccessRequestObservedDeparture = false;
			allFilesAccessRequestCompleted = false;
			return true;
		}
	}

	private static void observeAllFilesAccessRequestDeparture()
	{
		synchronized (AllFilesAccessRequestLock)
		{
			if (allFilesAccessRequestPending)
			{
				allFilesAccessRequestObservedDeparture = true;
			}
		}
	}

	private static void completeAllFilesAccessRequest()
	{
		synchronized (AllFilesAccessRequestLock)
		{
			allFilesAccessRequestPending = false;
			allFilesAccessRequestObservedDeparture = false;
			allFilesAccessRequestCompleted = true;
		}
	}

	private static void completeReturnedAllFilesAccessRequest()
	{
		synchronized (AllFilesAccessRequestLock)
		{
			if (!allFilesAccessRequestPending ||
				!allFilesAccessRequestObservedDeparture)
			{
				return;
			}
			allFilesAccessRequestPending = false;
			allFilesAccessRequestObservedDeparture = false;
			allFilesAccessRequestCompleted = true;
		}
	}

	/**
	 * 消费一次授权流程完成信号。该信号只表示权限页/权限对话框已经返回，
	 * native 侧仍需调用 isAllFilesAccessGranted() 获取最终授权结果。
	 */
	public static boolean consumeAllFilesAccessRequestCompleted()
	{
		synchronized (AllFilesAccessRequestLock)
		{
			boolean completed = allFilesAccessRequestCompleted;
			allFilesAccessRequestCompleted = false;
			return completed;
		}
	}

	/**
	 * 查询当前应用是否已获得"所有文件访问权限"。
	 * Android 10 及以下检查传统的 READ_EXTERNAL_STORAGE 运行时权限。
	 */
	public static boolean isAllFilesAccessGranted()
	{
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
		{
			SDLActivity activity = mSingleton;
			return activity != null
				&& activity.checkSelfPermission(
					Manifest.permission.READ_EXTERNAL_STORAGE)
					== PackageManager.PERMISSION_GRANTED;
		}
		return Environment.isExternalStorageManager();
	}

	/**
	 * 跳转系统"所有文件访问权限"设置页，由用户手动开启。
	 * Android 10 及以下改为申请 READ_EXTERNAL_STORAGE 运行时权限。
	 */
	public static void requestAllFilesAccess()
	{
		if (!beginAllFilesAccessRequest())
		{
			return;
		}
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			completeAllFilesAccessRequest();
			return;
		}
		try
		{
			activity.runOnUiThread(new Runnable()
			{
				@Override
				public void run()
				{
					requestAllFilesAccessOnUiThread(activity);
				}
			});
		}
		catch (Exception exception)
		{
			completeAllFilesAccessRequest();
		}
	}

	private static void requestAllFilesAccessOnUiThread(SDLActivity activity)
	{
		if (isAllFilesAccessGranted())
		{
			completeAllFilesAccessRequest();
			return;
		}
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
		{
			try
			{
				activity.requestPermissions(
					new String[] { Manifest.permission.READ_EXTERNAL_STORAGE },
					ExternalStoragePermissionRequestCode);
			}
			catch (Exception exception)
			{
				completeAllFilesAccessRequest();
			}
			return;
		}

		Intent applicationAllFilesAccess = new Intent(
			Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
			Uri.parse("package:" + activity.getPackageName()));
		if (startSettingsActivity(activity, applicationAllFilesAccess))
		{
			return;
		}

		Intent globalAllFilesAccess = new Intent(
			Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
		if (startSettingsActivity(activity, globalAllFilesAccess))
		{
			return;
		}

		Intent applicationDetails = new Intent(
			Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
			Uri.parse("package:" + activity.getPackageName()));
		if (!startSettingsActivity(activity, applicationDetails))
		{
			// 所有设置入口都不可用时也必须结束等待，避免 native 永久挂起。
			completeAllFilesAccessRequest();
		}
	}

	private static boolean startSettingsActivity(
		SDLActivity activity,
		Intent intent)
	{
		try
		{
			activity.startActivity(intent);
			return true;
		}
		catch (Exception exception)
		{
			return false;
		}
	}

	@Override
	protected void onPause()
	{
		observeAllFilesAccessRequestDeparture();
		super.onPause();
	}

	@Override
	protected void onResume()
	{
		super.onResume();
		completeReturnedAllFilesAccessRequest();
		resumePendingProgramInstall();
	}

	@Override
	public void onWindowFocusChanged(boolean hasFocus)
	{
		super.onWindowFocusChanged(hasFocus);
		if (hasFocus)
		{
			completeReturnedAllFilesAccessRequest();
		}
		else
		{
			observeAllFilesAccessRequestDeparture();
		}
	}

	@Override
	public void onRequestPermissionsResult(
		int requestCode,
		String[] permissions,
		int[] grantResults)
	{
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		if (requestCode == ExternalStoragePermissionRequestCode)
		{
			completeAllFilesAccessRequest();
		}
	}

	/**
	 * 获取公开外部存储根目录的绝对路径（如 /storage/emulated/0/）。
	 * 失败返回空字符串。注意：访问该根下的公开子目录在 Android 11+ 需要
	 * MANAGE_EXTERNAL_STORAGE（见 isAllFilesAccessGranted）。
	 */
	public static String getPublicStorageRoot()
	{
		try
		{
			File root = Environment.getExternalStorageDirectory();
			if (root == null)
			{
				return "";
			}
			return root.getAbsolutePath();
		}
		catch (Exception exception)
		{
			return "";
		}
	}

	/**
	 * 返回当前网络计费状态：0=未知或离线，1=非计费网络，2=计费网络。
	 * native 仅在真正下载大型资源或安装包前使用该结果显示流量提示。
	 */
	@SuppressWarnings("deprecation")
	public static int getActiveNetworkCost()
	{
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			return 0;
		}
		try
		{
			ConnectivityManager manager = (ConnectivityManager)
				activity.getSystemService(Context.CONNECTIVITY_SERVICE);
			if (manager == null)
			{
				return 0;
			}
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
			{
				Network network = manager.getActiveNetwork();
				NetworkCapabilities capabilities = network == null
					? null : manager.getNetworkCapabilities(network);
				if (capabilities == null ||
					!capabilities.hasCapability(
						NetworkCapabilities.NET_CAPABILITY_INTERNET))
				{
					return 0;
				}
				return capabilities.hasCapability(
					NetworkCapabilities.NET_CAPABILITY_NOT_METERED) ? 1 : 2;
			}
			NetworkInfo networkInfo = manager.getActiveNetworkInfo();
			if (networkInfo == null || !networkInfo.isConnected())
			{
				return 0;
			}
			return manager.isActiveNetworkMetered() ? 2 : 1;
		}
		catch (Exception exception)
		{
			return 0;
		}
	}

	/**
	 * 获取应用专属外部资源集合目录。该目录位于
	 * /storage/emulated/0/Android/data/<包名>/files/assets，访问它不需要
	 * 公共存储权限。目录由 native 安装事务按需创建。
	 */
	public static String getApplicationResourceDirectoryPath()
	{
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			return "";
		}
		try
		{
			File filesDirectory = activity.getExternalFilesDir(null);
			if (filesDirectory == null)
			{
				return "";
			}
			return new File(filesDirectory, "assets").getAbsolutePath();
		}
		catch (Exception exception)
		{
			return "";
		}
	}

	/**
	 * 获取应用专属主程序更新目录：
	 * getExternalFilesDir(DIRECTORY_DOWNLOADS) + "/updates"。
	 */
	public static String getApplicationProgramUpdateDirectoryPath()
	{
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			return "";
		}
		try
		{
			File downloads = activity.getExternalFilesDir(
				Environment.DIRECTORY_DOWNLOADS);
			if (downloads == null)
			{
				return "";
			}
			return new File(downloads, "updates").getAbsolutePath();
		}
		catch (Exception exception)
		{
			return "";
		}
	}

	/**
	 * 使用 PackageInstaller 请求安装应用专属更新目录中的 APK。
	 * 返回 true 只表示请求已交给 UI 线程；系统仍会要求用户确认。
	 */
	public static boolean requestProgramPackageInstall(byte[] pathUtf8)
	{
		SDLActivity activity = mSingleton;
		if (activity == null || pathUtf8 == null || pathUtf8.length == 0)
		{
			return false;
		}
		final File apkFile;
		try
		{
			String updatePath = getApplicationProgramUpdateDirectoryPath();
			if (updatePath.isEmpty())
			{
				return false;
			}
			File updateDirectory = new File(
				updatePath).getCanonicalFile();
			apkFile = new File(
				new String(pathUtf8, StandardCharsets.UTF_8)).getCanonicalFile();
			File expectedFile = new File(
				updateDirectory, "jxqy-update.apk").getCanonicalFile();
			if (!apkFile.equals(expectedFile) || !apkFile.isFile())
			{
				return false;
			}
		}
		catch (Exception exception)
		{
			return false;
		}
		try
		{
			activity.runOnUiThread(new Runnable()
			{
				@Override
				public void run()
				{
					beginProgramPackageInstallOnUiThread(activity, apkFile);
				}
			});
			return true;
		}
		catch (Exception exception)
		{
			return false;
		}
	}

	private static void beginProgramPackageInstallOnUiThread(
		SDLActivity activity,
		File apkFile)
	{
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O &&
			!activity.getPackageManager().canRequestPackageInstalls())
		{
			pendingProgramInstallFile = apkFile;
			try
			{
				activity.startActivity(new Intent(
					Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
					Uri.parse("package:" + activity.getPackageName())));
			}
			catch (Exception exception)
			{
				pendingProgramInstallFile = null;
				showProgramInstallMessage(activity, "无法打开应用安装权限设置");
			}
			return;
		}
		pendingProgramInstallFile = null;
		startProgramPackageInstall(activity, apkFile);
	}

	private void resumePendingProgramInstall()
	{
		if (pendingProgramInstallFile == null)
		{
			return;
		}
		File apkFile = pendingProgramInstallFile;
		pendingProgramInstallFile = null;
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O ||
			getPackageManager().canRequestPackageInstalls())
		{
			startProgramPackageInstall(this, apkFile);
		}
		else
		{
			showProgramInstallMessage(this, "未允许安装未知应用，更新未开始");
		}
	}

	private static void startProgramPackageInstall(
		SDLActivity activity,
		File apkFile)
	{
		Thread installerThread = new Thread(
			new Runnable()
			{
				@Override
				public void run()
				{
					commitProgramPackageInstall(activity, apkFile);
				}
			},
			"jxqy-package-installer");
		installerThread.start();
	}

	private static void commitProgramPackageInstall(
		SDLActivity activity,
		File apkFile)
	{
		PackageInstaller installer =
			activity.getPackageManager().getPackageInstaller();
		int sessionId = -1;
		try
		{
			PackageInstaller.SessionParams parameters =
				new PackageInstaller.SessionParams(
					PackageInstaller.SessionParams.MODE_FULL_INSTALL);
			parameters.setAppPackageName(activity.getPackageName());
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
			{
				parameters.setInstallReason(PackageManager.INSTALL_REASON_USER);
			}
			sessionId = installer.createSession(parameters);
			try (PackageInstaller.Session session =
				installer.openSession(sessionId))
			{
				try (FileInputStream input = new FileInputStream(apkFile);
					 OutputStream output = session.openWrite(
						"jxqy-update.apk", 0, apkFile.length()))
				{
					byte[] buffer = new byte[64 * 1024];
					int count;
					while ((count = input.read(buffer)) >= 0)
					{
						if (count > 0)
						{
							output.write(buffer, 0, count);
						}
					}
					session.fsync(output);
				}
				Intent statusIntent = new Intent(
					activity, JxqyPackageInstallReceiver.class);
				statusIntent.setAction(
					JxqyPackageInstallReceiver.ProgramInstallStatusAction);
				int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT;
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
				{
					pendingFlags |= PendingIntent.FLAG_MUTABLE;
				}
				PendingIntent statusPendingIntent = PendingIntent.getBroadcast(
					activity, sessionId, statusIntent, pendingFlags);
				session.commit(statusPendingIntent.getIntentSender());
			}
		}
		catch (Exception exception)
		{
			if (sessionId >= 0)
			{
				try
				{
					installer.abandonSession(sessionId);
				}
				catch (Exception ignored)
				{
				}
			}
			showProgramInstallMessage(activity, "无法启动系统安装确认");
		}
	}

	private static void showProgramInstallMessage(
		SDLActivity activity,
		String message)
	{
		activity.runOnUiThread(new Runnable()
		{
			@Override
			public void run()
			{
				Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
			}
		});
	}

	/**
	 * 供 native 工作线程逐块读取一个 HTTPS 响应。重定向由这里显式处理，
	 * 只允许继续跳转到 HTTPS；证书和主机名继续使用系统默认校验。
	 */
	public static Object openHttpsDownload(byte[] urlUtf8)
		throws IOException
	{
		if (urlUtf8 == null || urlUtf8.length == 0)
		{
			throw new IOException("Empty HTTPS URL");
		}
		URL currentUrl = new URL(
			new String(urlUtf8, StandardCharsets.UTF_8));
		for (int redirectCount = 0;
			redirectCount <= MaximumHttpsRedirects;
			redirectCount++)
		{
			if (!"https".equalsIgnoreCase(currentUrl.getProtocol()))
			{
				throw new IOException("Non-HTTPS URL rejected");
			}
			URLConnection rawConnection = currentUrl.openConnection();
			if (!(rawConnection instanceof HttpsURLConnection))
			{
				throw new IOException("HTTPS connection unavailable");
			}
			HttpsURLConnection connection =
				(HttpsURLConnection) rawConnection;
			connection.setInstanceFollowRedirects(false);
			connection.setRequestMethod("GET");
			connection.setRequestProperty(
				"User-Agent", "jxqy-all-in-one/1.0");
			connection.setConnectTimeout(10000);
			connection.setReadTimeout(30000);
			int responseCode = connection.getResponseCode();
			if (responseCode == HttpURLConnection.HTTP_MOVED_PERM ||
				responseCode == HttpURLConnection.HTTP_MOVED_TEMP ||
				responseCode == HttpURLConnection.HTTP_SEE_OTHER ||
				responseCode == 307 || responseCode == 308)
			{
				String location = connection.getHeaderField("Location");
				connection.disconnect();
				if (location == null || location.isEmpty() ||
					redirectCount == MaximumHttpsRedirects)
				{
					throw new IOException("HTTPS redirect rejected");
				}
				currentUrl = new URL(currentUrl, location);
				continue;
			}
			InputStream input = responseCode == HttpURLConnection.HTTP_OK
				? connection.getInputStream() : null;
			return new HttpsDownloadStream(
				connection, input, responseCode,
				connection.getContentLengthLong());
		}
		throw new IOException("Too many HTTPS redirects");
	}

	/** Native 侧持有并在同一工作线程中读取的短生命周期响应对象。 */
	public static final class HttpsDownloadStream
	{
		private final HttpsURLConnection connection;
		private final InputStream input;
		private final int httpStatus;
		private final long contentLength;

		private HttpsDownloadStream(
			HttpsURLConnection connection,
			InputStream input,
			int httpStatus,
			long contentLength)
		{
			this.connection = connection;
			this.input = input;
			this.httpStatus = httpStatus;
			this.contentLength = contentLength;
		}

		public int getHttpStatus()
		{
			return httpStatus;
		}

		public long getContentLength()
		{
			return contentLength;
		}

		public int read(byte[] buffer) throws IOException
		{
			if (input == null || buffer == null || buffer.length == 0)
			{
				return -1;
			}
			return input.read(buffer);
		}

		public void close()
		{
			if (input != null)
			{
				try
				{
					input.close();
				}
				catch (IOException ignored)
				{
				}
			}
			connection.disconnect();
		}
	}

	/**
	 * 获取应用包名，供 native 侧诊断/日志使用。
	 */
	public static String getApplicationPackageName()
	{
		SDLActivity activity = mSingleton;
		if (activity == null)
		{
			return "";
		}
		try
		{
			return activity.getPackageName();
		}
		catch (Exception exception)
		{
			return "";
		}
	}
}
