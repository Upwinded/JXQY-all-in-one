package com.upwinded.jxqy;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.nio.charset.StandardCharsets;
import java.security.cert.Certificate;
import java.util.ArrayDeque;
import java.util.zip.CRC32;
import javax.net.ssl.HttpsURLConnection;

public final class HttpsDownloadStreamTest
{
	private static final ArrayDeque<Response> responses = new ArrayDeque<>();

	private static final class Response extends HttpsURLConnection
	{
		int status = 206;
		String range = "bytes 4-7/8";
		String location;
		String encoding;
		boolean disconnected;
		boolean failOpen;
		Response() throws Exception { super(new URL("https://test.invalid/file")); }
		@Override public void connect() {}
		@Override public void disconnect() { disconnected = true; }
		@Override public boolean usingProxy() { return false; }
		@Override public String getCipherSuite() { return "test"; }
		@Override public Certificate[] getLocalCertificates() { return null; }
		@Override public Certificate[] getServerCertificates() { return null; }
		@Override public int getResponseCode() throws IOException
		{
			if (failOpen) { throw new IOException("network changed"); }
			return status;
		}
		@Override public String getHeaderField(String name)
		{
			return "Content-Range".equals(name) ? range : "Location".equals(name) ? location
				: "Content-Encoding".equals(name) ? encoding : null;
		}
		@Override public long getContentLengthLong() { return 4; }
		@Override public ByteArrayInputStream getInputStream()
		{
			return new ByteArrayInputStream("EFGH".getBytes(StandardCharsets.UTF_8));
		}
	}

	private static HttpsDownloadStream open() throws IOException
	{
		return HttpsDownloadStream.open("https://test.invalid/file".getBytes(StandardCharsets.UTF_8), 4, 8);
	}

	private static void check(boolean condition, String message)
	{
		if (!condition) { throw new AssertionError(message); }
	}

	public static void main(String[] args) throws Exception
	{
		if (args.length == 4)
		{
			long cutAt = Long.parseLong(args[1]);
			long expected = Long.parseLong(args[2]);
			long received = 0;
			CRC32 checksum = new CRC32();
			for (int connection = 0; connection < 2; ++connection)
			{
				try (HttpsDownloadStream stream = HttpsDownloadStream.open(
					args[0].getBytes(StandardCharsets.UTF_8), received, expected))
				{
					check(stream.getHttpStatus() == (connection == 0 ? 200 : 206) &&
						stream.getResponseOffset() == received, "live response starts at the saved offset");
					byte[] bytes = new byte[32768];
					int count;
					while ((count = stream.read(bytes)) >= 0)
					{
						checksum.update(bytes, 0, count);
						received += count;
						if (connection == 0 && received >= cutAt) break;
					}
					System.out.println("connection=" + connection + " status=" + stream.getHttpStatus() +
						" received=" + received);
				}
			}
			check(received == expected && checksum.getValue() == Long.parseLong(args[3], 16),
				"interrupted and resumed live artifact matches catalog size and CRC32");
			System.out.println("Live resume passed: bytes=" + received + " CRC32=" + Long.toHexString(checksum.getValue()));
			return;
		}
		if (args.length == 3)
		{
			try (HttpsDownloadStream stream = HttpsDownloadStream.open(args[0].getBytes(StandardCharsets.UTF_8),
				Long.parseLong(args[1]), Long.parseLong(args[2])))
			{
				byte[] bytes = new byte[32];
				System.out.println("status=" + stream.getHttpStatus() + " offset=" + stream.getResponseOffset() +
					" length=" + stream.getContentLength() + " read=" + stream.read(bytes));
			}
			return;
		}
		URL.setURLStreamHandlerFactory(protocol -> "https".equals(protocol) ? new URLStreamHandler()
		{
			@Override protected URLConnection openConnection(URL url) { return responses.removeFirst(); }
		} : null);
		check(HttpsDownloadStream.validatedRangeOffset("bytes 4-7/8", 4, 8, 4) == 4, "valid range");
		for (String range : new String[] { null, "bytes 0-7/8", "bytes 4-8/8", "bytes 4-7/9", "bytes */8",
			"bytes 4-3/8", "bytes 4-7/*", "bytes 4-7/999999999999999999999999" })
		{
			check(HttpsDownloadStream.validatedRangeOffset(range, 4, 8, 4) == -1, "reject invalid range " + range);
		}
		check(HttpsDownloadStream.validatedRangeOffset("bytes 4-7/8", 4, 8, 5) == -1, "length mismatch");
		Response redirected = new Response();
		redirected.status = 302;
		redirected.location = "https://mirror.invalid/file";
		Response partial = new Response();
		responses.add(redirected);
		responses.add(partial);
		try (HttpsDownloadStream stream = open())
		{
			byte[] suffix = new byte[4];
			check(stream.getHttpStatus() == 206 && stream.getResponseOffset() == 4 && stream.read(suffix) == 4 &&
				"EFGH".equals(new String(suffix, StandardCharsets.UTF_8)), "read exact suffix");
			check("bytes=4-".equals(partial.getRequestProperty("Range")), "range survives redirect");
			check("identity".equals(partial.getRequestProperty("Accept-Encoding")), "disable transparent decoding");
		}
		check(redirected.disconnected && partial.disconnected, "close redirected and completed connections");
		Response ignored = new Response();
		ignored.status = 200;
		responses.add(ignored);
		try (HttpsDownloadStream stream = open())
		{
			check(stream.getResponseOffset() == 0, "full response is distinguishable and must not be appended");
		}
		Response wrong = new Response();
		wrong.range = "bytes 0-3/8";
		responses.add(wrong);
		try (HttpsDownloadStream stream = open())
		{
			check(stream.getResponseOffset() == -1 && stream.read(new byte[4]) == -1, "wrong range exposes no body");
		}
		Response failed = new Response();
		Response encoded = new Response();
		encoded.encoding = "gzip";
		responses.add(encoded);
		try (HttpsDownloadStream stream = open())
		{
			check(stream.getResponseOffset() == -1 && stream.read(new byte[4]) == -1,
				"encoded byte ranges cannot be appended to a ZIP prefix");
		}
		failed.failOpen = true;
		responses.add(failed);
		try { open(); throw new AssertionError("expected connection failure"); }
		catch (IOException expected) { check(failed.disconnected, "failed connection is closed"); }
		Response downgrade = new Response();
		downgrade.status = 302;
		downgrade.location = "http://test.invalid/file";
		responses.add(downgrade);
		try { open(); throw new AssertionError("expected HTTPS downgrade rejection"); }
		catch (IOException expected) { check(downgrade.disconnected, "downgrade connection is closed"); }
		System.out.println("Android HTTPS transport checks passed");
	}
}
