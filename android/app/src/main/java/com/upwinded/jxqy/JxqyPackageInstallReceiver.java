package com.upwinded.jxqy;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Environment;
import android.widget.Toast;

import java.io.File;

/** Receives PackageInstaller results without exposing an exported component. */
public final class JxqyPackageInstallReceiver extends BroadcastReceiver
{
	public static final String ProgramInstallStatusAction =
		"com.upwinded.jxqy.PROGRAM_INSTALL_STATUS";

	@Override
	public void onReceive(Context context, Intent intent)
	{
		if (intent == null ||
			!ProgramInstallStatusAction.equals(intent.getAction()))
		{
			return;
		}
		int status = intent.getIntExtra(
			PackageInstaller.EXTRA_STATUS,
			PackageInstaller.STATUS_FAILURE);
		if (status == PackageInstaller.STATUS_PENDING_USER_ACTION)
		{
			Intent confirmation = (Intent) intent.getParcelableExtra(
				Intent.EXTRA_INTENT);
			if (confirmation != null)
			{
				try
				{
					confirmation.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
					context.startActivity(confirmation);
					return;
				}
				catch (Exception ignored)
				{
				}
			}
			showMessage(context, "无法打开系统安装确认");
		}
		else if (status == PackageInstaller.STATUS_SUCCESS)
		{
			File downloads = context.getExternalFilesDir(
				Environment.DIRECTORY_DOWNLOADS);
			if (downloads != null)
			{
				File updateDirectory = new File(downloads, "updates");
				File apkFile = new File(updateDirectory, "jxqy-update.apk");
				if (apkFile.isFile() && !apkFile.delete())
				{
					showMessage(context, "安装完成，但旧安装包清理失败");
					return;
				}
				String[] remainingFiles = updateDirectory.list();
				if (remainingFiles != null && remainingFiles.length == 0)
				{
					updateDirectory.delete();
				}
			}
		}
		else
		{
			String message = intent.getStringExtra(
				PackageInstaller.EXTRA_STATUS_MESSAGE);
			showMessage(
				context,
				message == null || message.isEmpty()
					? "主程序安装未完成" : message);
		}
	}

	private static void showMessage(Context context, String message)
	{
		Toast.makeText(context, message, Toast.LENGTH_LONG).show();
	}
}
