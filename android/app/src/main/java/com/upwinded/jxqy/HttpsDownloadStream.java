package com.upwinded.jxqy;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.net.ssl.HttpsURLConnection;

/** HTTPS transport shared by native downloads and the host transport tests. */
public final class HttpsDownloadStream implements AutoCloseable
{
	private static final int MaximumHttpsRedirects = 5;
	private static final Pattern ContentRange =
		Pattern.compile("bytes ([0-9]+)-([0-9]+)/([0-9]+)");
	private final HttpsURLConnection connection;
	private final InputStream input;
	private final int httpStatus;
	private final long contentLength;
	private final long responseOffset;

	private HttpsDownloadStream(HttpsURLConnection connection, InputStream input,
		int httpStatus, long contentLength, long responseOffset)
	{
		this.connection = connection;
		this.input = input;
		this.httpStatus = httpStatus;
		this.contentLength = contentLength;
		this.responseOffset = responseOffset;
	}

	public static HttpsDownloadStream open(byte[] urlUtf8, long offset, long expectedBytes)
		throws IOException
	{
		if (urlUtf8 == null || urlUtf8.length == 0 || offset < 0 ||
			(offset > 0 && offset >= expectedBytes))
		{
			throw new IOException("Invalid HTTPS download request");
		}
		URL currentUrl = new URL(new String(urlUtf8, StandardCharsets.UTF_8));
		for (int redirectCount = 0; redirectCount <= MaximumHttpsRedirects; redirectCount++)
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
			HttpsURLConnection connection = (HttpsURLConnection) rawConnection;
			boolean streamOwnsConnection = false;
			try
			{
				connection.setInstanceFollowRedirects(false);
				connection.setRequestMethod("GET");
				connection.setRequestProperty("User-Agent", "jxqy-all-in-one/1.0");
				// Byte offsets refer to the ZIP, never a transparently decoded response.
				connection.setRequestProperty("Accept-Encoding", "identity");
				if (offset > 0)
				{
					connection.setRequestProperty("Range", "bytes=" + offset + "-");
				}
				connection.setConnectTimeout(10000);
				connection.setReadTimeout(15000);
				int responseCode = connection.getResponseCode();
				if (responseCode == HttpURLConnection.HTTP_MOVED_PERM ||
					responseCode == HttpURLConnection.HTTP_MOVED_TEMP ||
					responseCode == HttpURLConnection.HTTP_SEE_OTHER ||
					responseCode == 307 || responseCode == 308)
				{
					String location = connection.getHeaderField("Location");
					if (location == null || location.isEmpty() || redirectCount == MaximumHttpsRedirects)
					{
						throw new IOException("HTTPS redirect rejected");
					}
					currentUrl = new URL(currentUrl, location);
					continue;
				}
				long length = connection.getContentLengthLong();
				long responseOffset = responseCode == HttpURLConnection.HTTP_OK ? 0 : -1;
				String encoding = connection.getHeaderField("Content-Encoding");
				boolean identity = encoding == null || "identity".equalsIgnoreCase(encoding);
				if (responseCode == HttpURLConnection.HTTP_PARTIAL && identity)
				{
					responseOffset = validatedRangeOffset(
						connection.getHeaderField("Content-Range"), offset, expectedBytes, length);
				}
				if (!identity)
				{
					responseOffset = -1;
				}
				InputStream input = responseOffset >= 0 &&
					(responseCode == HttpURLConnection.HTTP_OK ||
					 responseCode == HttpURLConnection.HTTP_PARTIAL)
					? connection.getInputStream() : null;
				HttpsDownloadStream stream = new HttpsDownloadStream(
					connection, input, responseCode, length, responseOffset);
				streamOwnsConnection = true;
				return stream;
			}
			finally
			{
				if (!streamOwnsConnection)
				{
					connection.disconnect();
				}
			}
		}
		throw new IOException("Too many HTTPS redirects");
	}

	static long validatedRangeOffset(String header, long offset, long expectedBytes, long length)
	{
		if (header == null || offset <= 0 || expectedBytes <= offset)
		{
			return -1;
		}
		Matcher match = ContentRange.matcher(header);
		if (!match.matches())
		{
			return -1;
		}
		try
		{
			long start = Long.parseLong(match.group(1));
			long end = Long.parseLong(match.group(2));
			long total = Long.parseLong(match.group(3));
			return start == offset && total == expectedBytes && end >= start && end < total &&
				(length < 0 || length == end - start + 1) ? start : -1;
		}
		catch (NumberFormatException exception)
		{
			return -1;
		}
	}

	public int getHttpStatus() { return httpStatus; }
	public long getContentLength() { return contentLength; }
	public long getResponseOffset() { return responseOffset; }

	public int read(byte[] buffer) throws IOException
	{
		return input == null || buffer == null || buffer.length == 0 ? -1 : input.read(buffer);
	}

	@Override
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
