// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.externalnav;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.SystemClock;
import android.provider.Browser;
import android.support.customtabs.CustomTabsIntent;
import android.test.InstrumentationTestCase;
import android.test.mock.MockContext;
import android.test.mock.MockPackageManager;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.CommandLine;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.externalnav.ExternalNavigationHandler.OverrideUrlLoadingResult;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabRedirectHandler;
import org.chromium.chrome.browser.util.FeatureUtilities;
import org.chromium.ui.base.PageTransition;
import org.chromium.webapk.lib.common.WebApkConstants;

import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.List;

/**
 * Instrumentation tests for {@link ExternalNavigationHandler}.
 */
public class ExternalNavigationHandlerTest extends InstrumentationTestCase {

    // Expectations
    private static final int IGNORE = 0x0;
    private static final int START_INCOGNITO = 0x1;
    private static final int START_CHROME = 0x2;
    private static final int START_WEBAPK = 0x4;
    private static final int START_FILE = 0x8;
    private static final int START_OTHER_ACTIVITY = 0x10;
    private static final int INTENT_SANITIZATION_EXCEPTION = 0x20;

    private static final String SEARCH_RESULT_URL_FOR_TOM_HANKS =
            "https://www.google.com/search?q=tom+hanks";
    private static final String IMDB_WEBPAGE_FOR_TOM_HANKS = "http://m.imdb.com/name/nm0000158";
    private static final String INTENT_URL_WITH_FALLBACK_URL =
            "intent:///name/nm0000158#Intent;scheme=imdb;package=com.imdb.mobile;"
            + "S." + ExternalNavigationHandler.EXTRA_BROWSER_FALLBACK_URL + "="
            + Uri.encode(IMDB_WEBPAGE_FOR_TOM_HANKS) + ";end";
    private static final String INTENT_URL_WITH_FALLBACK_URL_WITHOUT_PACKAGE_NAME =
            "intent:///name/nm0000158#Intent;scheme=imdb;"
            + "S." + ExternalNavigationHandler.EXTRA_BROWSER_FALLBACK_URL + "="
            + Uri.encode(IMDB_WEBPAGE_FOR_TOM_HANKS) + ";end";
    private static final String SOME_JAVASCRIPT_PAGE = "javascript:window.open(0);";
    private static final String INTENT_URL_WITH_JAVASCRIPT_FALLBACK_URL =
            "intent:///name/nm0000158#Intent;scheme=imdb;package=com.imdb.mobile;"
            + "S." + ExternalNavigationHandler.EXTRA_BROWSER_FALLBACK_URL + "="
            + Uri.encode(SOME_JAVASCRIPT_PAGE) + ";end";
    private static final String IMDB_APP_INTENT_FOR_TOM_HANKS = "imdb:///name/nm0000158";
    private static final String INTENT_URL_WITH_CHAIN_FALLBACK_URL =
            "intent://scan/#Intent;scheme=zxing;"
            + "S." + ExternalNavigationHandler.EXTRA_BROWSER_FALLBACK_URL + "="
            + Uri.encode("http://url.myredirector.com/aaa") + ";end";
    private static final String ENCODED_MARKET_REFERRER =
            "_placement%3D{placement}%26network%3D{network}%26device%3D{devicemodel}";
    private static final String INTENT_APP_NOT_INSTALLED_DEFAULT_MARKET_REFERRER =
            "intent:///name/nm0000158#Intent;scheme=imdb;package=com.imdb.mobile;end";
    private static final String INTENT_APP_NOT_INSTALLED_WITH_MARKET_REFERRER =
            "intent:///name/nm0000158#Intent;scheme=imdb;package=com.imdb.mobile;S."
            + ExternalNavigationHandler.EXTRA_MARKET_REFERRER + "="
            + ENCODED_MARKET_REFERRER + ";end";

    private static final String PLUS_STREAM_URL = "https://plus.google.com/stream";
    private static final String CALENDAR_URL = "http://www.google.com/calendar";
    private static final String KEEP_URL = "http://www.google.com/keep";

    private static final String TEXT_APP_1_PACKAGE_NAME = "text_app_1";
    private static final String TEXT_APP_2_PACKAGE_NAME = "text_app_2";

    private static final String WEBAPK_SCOPE = "https://www.template.com";
    private static final String WEBAPK_PACKAGE_NAME = "org.chromium.webapk.template";

    private static final String WEBAPK_WITH_NATIVE_APP_SCOPE =
            "https://www.webapk.with.native.com";
    private static final String WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME =
            "org.chromium.webapk.with.native";
    private static final String NATIVE_APP_PACKAGE_NAME = "com.webapk.with.native.android";

    private static final String COUNTERFEIT_WEBAPK_SCOPE = "http://www.counterfeit.webapk.com";
    private static final String COUNTERFEIT_WEBAPK_PACKAGE_NAME =
            "org.chromium.webapk.counterfeit";

    private final TestExternalNavigationDelegate mDelegate;
    private ExternalNavigationHandler mUrlHandler;

    public ExternalNavigationHandlerTest() {
        mDelegate = new TestExternalNavigationDelegate();
        mUrlHandler = new ExternalNavigationHandler(mDelegate);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mDelegate.setContext(getInstrumentation().getTargetContext());
        CommandLine.init(new String[0]);
        RecordHistogram.disableForTests();
        mDelegate.mQueryIntentOverride = null;
    }

    @SmallTest
    public void testOrdinaryIncognitoUri() {
        checkUrl("http://youtube.com/")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_ASYNC_ACTION, START_INCOGNITO);
    }

    @SmallTest
    public void testChromeReferrer() {
        // http://crbug.com/159153: Don't override http or https URLs from the NTP or bookmarks.
        checkUrl("http://youtube.com/")
                .withReferrer("chrome://about")
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl("tel:012345678")
                .withReferrer("chrome://about")
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testForwardBackNavigation() {
        // http://crbug.com/164194. We shouldn't show the intent picker on
        // forwards or backwards navigations.
        checkUrl("http://youtube.com/")
                .withPageTransition(PageTransition.LINK | PageTransition.FORWARD_BACK)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testRedirectFromFormSubmit() {
        // http://crbug.com/181186: We need to show the intent picker when we receive a redirect
        // following a form submit. OAuth of native applications rely on this.
        checkUrl("market://1234")
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
        checkUrl("http://youtube.com://")
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // If the page matches the referrer, then continue loading in Chrome.
        checkUrl("http://youtube.com://")
                .withReferrer("http://youtube.com")
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // If the page does not match the referrer, then prompt an intent.
        checkUrl("http://youtube.com://")
                .withReferrer("http://google.com")
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // It doesn't make sense to allow intent picker without redirect, since form data
        // is not encoded in the intent (although, in theory, it could be passed in as
        // an extra data in the intent).
        checkUrl("http://youtube.com://")
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testIgnore() {
        // Ensure about: URLs are not broadcast for external navigation.
        checkUrl("about:test").expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl("about:test")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Ensure content: URLs are not broadcast for external navigation.
        checkUrl("content:test").expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl("content:test")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Ensure chrome: URLs are not broadcast for external navigation.
        checkUrl("chrome://history").expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl("chrome://history")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Ensure chrome-native: URLs are not broadcast for external navigation.
        checkUrl("chrome-native://newtab").expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl("chrome-native://newtab")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testPageTransitionType() {
        // Non-link page transition type are ignored.
        checkUrl("http://youtube.com/")
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
        checkUrl("http://youtube.com/")
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // http://crbug.com/143118 - Don't show the picker for directly typed URLs, unless
        // the URL results in a redirect.
        checkUrl("http://youtube.com/")
                .withPageTransition(PageTransition.TYPED)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // http://crbug.com/162106 - Don't show the picker on reload.
        checkUrl("http://youtube.com/")
                .withPageTransition(PageTransition.RELOAD)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testWtai() {
        // Start the telephone application with the given number.
        checkUrl("wtai://wp/mc;0123456789")
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY | INTENT_SANITIZATION_EXCEPTION);

        // These two cases are currently unimplemented.
        checkUrl("wtai://wp/sd;0123456789")
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE,
                        IGNORE | INTENT_SANITIZATION_EXCEPTION);
        checkUrl("wtai://wp/ap;0123456789")
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE,
                        IGNORE | INTENT_SANITIZATION_EXCEPTION);

        // Ignore other WTAI urls.
        checkUrl("wtai://wp/invalid")
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE,
                        IGNORE | INTENT_SANITIZATION_EXCEPTION);
    }

    @SmallTest
    public void testRedirectToMarketWithReferrer() {
        mDelegate.setCanResolveActivity(false);

        checkUrl(INTENT_APP_NOT_INSTALLED_WITH_MARKET_REFERRER)
                .withReferrer(KEEP_URL)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);


        assertNotNull(mDelegate.startActivityIntent);
        Uri uri = mDelegate.startActivityIntent.getData();
        assertEquals("market", uri.getScheme());
        assertEquals(Uri.decode(ENCODED_MARKET_REFERRER), uri.getQueryParameter("referrer"));
        assertEquals(Uri.parse(KEEP_URL),
                mDelegate.startActivityIntent.getParcelableExtra(Intent.EXTRA_REFERRER));
    }


    @SmallTest
    public void testRedirectToMarketWithoutReferrer() {
        mDelegate.setCanResolveActivity(false);

        checkUrl(INTENT_APP_NOT_INSTALLED_DEFAULT_MARKET_REFERRER)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        assertNotNull(mDelegate.startActivityIntent);
        Uri uri = mDelegate.startActivityIntent.getData();
        assertEquals("market", uri.getScheme());
        assertEquals(mDelegate.getPackageName(), uri.getQueryParameter("referrer"));
    }

    @SmallTest
    public void testExternalUri() {
        checkUrl("tel:012345678")
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testTypedRedirectToExternalProtocol() {
        // http://crbug.com/169549
        checkUrl("market://1234")
                .withPageTransition(PageTransition.TYPED)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // http://crbug.com/143118
        checkUrl("market://1234")
                .withPageTransition(PageTransition.TYPED)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testIncomingIntentRedirect() {
        int transitionTypeIncomingIntent = PageTransition.LINK
                | PageTransition.FROM_API;
        // http://crbug.com/149218
        checkUrl("http://youtube.com/")
                .withPageTransition(transitionTypeIncomingIntent)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // http://crbug.com/170925
        checkUrl("http://youtube.com/")
                .withPageTransition(transitionTypeIncomingIntent)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testIntentScheme() {
        String url = "intent:wtai://wp/#Intent;action=android.settings.SETTINGS;"
                + "component=package/class;end";
        String urlWithSel = "intent:wtai://wp/#Intent;SEL;action=android.settings.SETTINGS;"
                + "component=package/class;end";

        checkUrl(url).expecting(
                OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT, START_OTHER_ACTIVITY);

        // http://crbug.com/370399
        checkUrl(urlWithSel)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testYouTubePairingCode() {
        int transitionTypeIncomingIntent = PageTransition.LINK
                | PageTransition.FROM_API;
        String url = "http://m.youtube.com/watch?v=1234&pairingCode=5678";

        // http://crbug/386600 - it makes no sense to switch activities for pairing code URLs.
        checkUrl(url)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        checkUrl(url)
                .withPageTransition(transitionTypeIncomingIntent)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testInitialIntent() throws URISyntaxException {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(new TestContext());
        Intent ytIntent = Intent.parseUri("http://youtube.com/", Intent.URI_INTENT_SCHEME);
        Intent fooIntent = Intent.parseUri("http://foo.com/", Intent.URI_INTENT_SCHEME);
        int transTypeLinkFromIntent = PageTransition.LINK
                | PageTransition.FROM_API;

        // Ignore if url is redirected, transition type is IncomingIntent and a new intent doesn't
        // have any new resolver.
        redirectHandler.updateIntent(ytIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("http://m.youtube.com/")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Do not ignore if a new intent has any new resolver.
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("http://m.youtube.com/")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // Do not ignore if a new intent cannot be handled by Chrome.
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("intent://myownurl")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testInitialIntentHeadingToChrome() throws URISyntaxException {
        TestContext context = new TestContext();
        TabRedirectHandler redirectHandler = new TabRedirectHandler(context);
        Intent fooIntent = Intent.parseUri("http://foo.com/", Intent.URI_INTENT_SCHEME);
        fooIntent.setPackage(context.getPackageName());
        int transTypeLinkFromIntent = PageTransition.LINK
                | PageTransition.FROM_API;

        // Ignore if an initial Intent was heading to Chrome.
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("http://m.youtube.com/")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Do not ignore if the URI has an external protocol.
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("market://1234")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testIntentForCustomTab() throws URISyntaxException {
        TestContext context = new TestContext();
        TabRedirectHandler redirectHandler = new TabRedirectHandler(context);
        int transTypeLinkFromIntent = PageTransition.LINK | PageTransition.FROM_API;

        // In Custom Tabs, if the first url is not a redirect, stay in chrome.
        Intent barIntent = Intent.parseUri("http://youtube.com/", Intent.URI_INTENT_SCHEME);
        barIntent.putExtra(CustomTabsIntent.EXTRA_SESSION, "");
        barIntent.setPackage(context.getPackageName());
        redirectHandler.updateIntent(barIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        checkUrl("http://youtube.com/")
                .withPageTransition(transTypeLinkFromIntent)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // In Custom Tabs, if the first url is a redirect, allow it to intent out.
        Intent fooIntent = Intent.parseUri("http://foo.com/", Intent.URI_INTENT_SCHEME);
        fooIntent.putExtra(CustomTabsIntent.EXTRA_SESSION, "");
        fooIntent.setPackage(context.getPackageName());
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, true, false, 0, 0);
        checkUrl("http://youtube.com/")
                .withPageTransition(transTypeLinkFromIntent)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        // External intent for a user-initiated navigation should always be allowed.
        redirectHandler.updateIntent(fooIntent);
        redirectHandler.updateNewUrlLoading(transTypeLinkFromIntent, false, false, 0, 0);
        // Simulate a real user navigation.
        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, true,
                SystemClock.elapsedRealtime() + 1, 0);
        checkUrl("http://youtube.com/")
                .withPageTransition(PageTransition.LINK)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testFallbackUrl_IntentResolutionSucceeds() {
        // IMDB app is installed.
        mDelegate.setCanResolveActivity(true);

        checkUrl(INTENT_URL_WITH_FALLBACK_URL)
                .withReferrer(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        Intent invokedIntent = mDelegate.startActivityIntent;
        assertEquals(IMDB_APP_INTENT_FOR_TOM_HANKS, invokedIntent.getData().toString());
        assertNull("The invoked intent should not have browser_fallback_url\n",
                invokedIntent.getStringExtra(ExternalNavigationHandler.EXTRA_BROWSER_FALLBACK_URL));
        assertNull(mDelegate.getNewUrlAfterClobbering());
        assertNull(mDelegate.getReferrerUrlForClobbering());
    }

    @SmallTest
    public void testFallbackUrl_IntentResolutionFails() {
        // IMDB app isn't installed.
        mDelegate.setCanResolveActivity(false);

        // When intent resolution fails, we should not start an activity, but instead clobber
        // the current tab.
        checkUrl(INTENT_URL_WITH_FALLBACK_URL)
                .withReferrer(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);

        assertNull(mDelegate.startActivityIntent);
        assertEquals(IMDB_WEBPAGE_FOR_TOM_HANKS, mDelegate.getNewUrlAfterClobbering());
        assertEquals(SEARCH_RESULT_URL_FOR_TOM_HANKS, mDelegate.getReferrerUrlForClobbering());
    }

    @SmallTest
    public void testFallbackUrl_IntentResolutionFailsWithoutPackageName() {
        // IMDB app isn't installed.
        mDelegate.setCanResolveActivity(false);

        // Fallback URL should work even when package name isn't given.
        checkUrl(INTENT_URL_WITH_FALLBACK_URL_WITHOUT_PACKAGE_NAME)
                .withReferrer(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);

        assertNull(mDelegate.startActivityIntent);
        assertEquals(IMDB_WEBPAGE_FOR_TOM_HANKS, mDelegate.getNewUrlAfterClobbering());
        assertEquals(SEARCH_RESULT_URL_FOR_TOM_HANKS, mDelegate.getReferrerUrlForClobbering());
    }

    @SmallTest
    public void testFallbackUrl_FallbackShouldNotWarnOnIncognito() {
        // IMDB app isn't installed.
        mDelegate.setCanResolveActivity(false);

        checkUrl(INTENT_URL_WITH_FALLBACK_URL)
                .withReferrer(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);

        assertNull(mDelegate.startActivityIntent);
        assertEquals(IMDB_WEBPAGE_FOR_TOM_HANKS, mDelegate.getNewUrlAfterClobbering());
        assertEquals(SEARCH_RESULT_URL_FOR_TOM_HANKS, mDelegate.getReferrerUrlForClobbering());
    }

    @SmallTest
    public void testFallbackUrl_IgnoreJavascriptFallbackUrl() {
        // IMDB app isn't installed.
        mDelegate.setCanResolveActivity(false);

        // Will be redirected market since package is given.
        checkUrl(INTENT_URL_WITH_JAVASCRIPT_FALLBACK_URL)
                .withReferrer(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .withIsIncognito(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        Intent invokedIntent = mDelegate.startActivityIntent;
        assertTrue(invokedIntent.getData().toString().startsWith("market://"));
        assertEquals(null, mDelegate.getNewUrlAfterClobbering());
        assertEquals(null, mDelegate.getReferrerUrlForClobbering());
    }

    @SmallTest
    public void testFallback_UseFallbackUrlForRedirectionFromTypedInUrl() {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(PageTransition.TYPED, false, false, 0, 0);
        checkUrl("http://goo.gl/abcdefg")
                .withPageTransition(PageTransition.TYPED)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 0, 0);
        checkUrl(INTENT_URL_WITH_FALLBACK_URL_WITHOUT_PACKAGE_NAME)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);

        // Now the user opens a link.
        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, true, 0, 1);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testIgnoreEffectiveRedirectFromIntentFallbackUrl() {
        // We cannot resolve any intent, so fall-back URL will be used.
        mDelegate.setCanResolveActivity(false);

        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, true, 0, 0);
        checkUrl(INTENT_URL_WITH_CHAIN_FALLBACK_URL)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);

        // As a result of intent resolution fallback, we have clobberred the current tab.
        // The fall-back URL was HTTP-schemed, but it was effectively redirected to a new intent
        // URL using javascript. However, we do not allow chained fallback intent, so we do NOT
        // override URL loading here.
        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 0, 0);
        checkUrl(INTENT_URL_WITH_FALLBACK_URL)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        // Now enough time (2 seconds) have passed.
        // New URL loading should not be affected.
        // (The URL happened to be the same as previous one.)
        // TODO(changwan): this is not likely cause flakiness, but it may be better to refactor
        // systemclock or pass the new time as parameter.
        long lastUserInteractionTimeInMillis = SystemClock.elapsedRealtime() + 2 * 1000L;
        redirectHandler.updateNewUrlLoading(
                PageTransition.LINK, false, true, lastUserInteractionTimeInMillis, 1);
        checkUrl(INTENT_URL_WITH_FALLBACK_URL)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB, IGNORE);
    }

    @SmallTest
    public void testIgnoreEffectiveRedirectFromUserTyping() {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(PageTransition.TYPED, false, false, 0, 0);
        checkUrl("http://m.youtube.com/")
                .withPageTransition(PageTransition.TYPED)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.TYPED, true, false, 0, 0);
        checkUrl("http://m.youtube.com/")
                .withPageTransition(PageTransition.TYPED)
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 0, 1);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testNavigationFromLinkWithoutUserGesture() {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, true, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 1, 1);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testChromeAppInBackground() {
        mDelegate.setIsChromeAppInForeground(false);
        checkUrl("http://youtube.com/").expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testNotChromeAppInForegroundRequired() {
        mDelegate.setIsChromeAppInForeground(false);
        checkUrl("http://youtube.com/")
                .withChromeAppInForegroundRequired(false)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testCreatesIntentsToOpenInNewTab() {
        mUrlHandler = new ExternalNavigationHandler(mDelegate);
        ExternalNavigationParams params = new ExternalNavigationParams.Builder(
                "http://m.youtube.com", false)
                .setOpenInNewTab(true)
                .build();
        OverrideUrlLoadingResult result = mUrlHandler.shouldOverrideUrlLoading(params);
        assertEquals(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT, result);
        assertTrue(mDelegate.startActivityIntent != null);
        assertTrue(mDelegate.startActivityIntent.getBooleanExtra(
                Browser.EXTRA_CREATE_NEW_TAB, false));
    }

    @SmallTest
    public void testCanExternalAppHandleUrl() {
        mDelegate.setCanResolveActivity(true);
        assertTrue(mUrlHandler.canExternalAppHandleUrl("some_app://some_app.com/"));

        mDelegate.setCanResolveActivity(false);
        assertTrue(mUrlHandler.canExternalAppHandleUrl("wtai://wp/mc;0123456789"));
        assertTrue(mUrlHandler.canExternalAppHandleUrl(
                "intent:/#Intent;scheme=no_app;package=com.no_app;end"));
        assertFalse(mUrlHandler.canExternalAppHandleUrl("no_app://no_app.com/"));
    }

    @SmallTest
    public void testPlusAppRefresh() {
        checkUrl(PLUS_STREAM_URL)
                .withReferrer(PLUS_STREAM_URL)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testSameDomainDifferentApps() {
        checkUrl(CALENDAR_URL)
                .withReferrer(KEEP_URL)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    @SmallTest
    public void testFormSubmitSameDomain() {
        checkUrl(CALENDAR_URL)
                .withReferrer(KEEP_URL)
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testBackgroundTabNavigation() {
        checkUrl("http://youtube.com/")
                .withIsBackgroundTabNavigation(true)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testReferrerExtra() {
        String referrer = "http://www.google.com";
        checkUrl("http://youtube.com:90/foo/bar")
                .withReferrer(referrer)
                .withPageTransition(PageTransition.FORM_SUBMIT)
                .withIsRedirect(true)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
        assertEquals(Uri.parse(referrer),
                mDelegate.startActivityIntent.getParcelableExtra(Intent.EXTRA_REFERRER));
        assertEquals(1, mDelegate.startActivityIntent.getIntExtra(
                IntentHandler.EXTRA_REFERRER_ID, 0));
    }

    @SmallTest
    public void testNavigationFromReload() {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(PageTransition.RELOAD, false, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, true, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 1, 1);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SmallTest
    public void testNavigationWithForwardBack() {
        TabRedirectHandler redirectHandler = new TabRedirectHandler(null);

        redirectHandler.updateNewUrlLoading(
                PageTransition.FORM_SUBMIT | PageTransition.FORWARD_BACK, false, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, true, false, 1, 0);
        checkUrl("http://m.youtube.com/")
                .withIsRedirect(true)
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        redirectHandler.updateNewUrlLoading(PageTransition.LINK, false, false, 1, 1);
        checkUrl("http://m.youtube.com/")
                .withRedirectHandler(redirectHandler)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    @SuppressLint("SdCardPath")
    @SmallTest
    public void testFileAccess() {
        String fileUrl = "file:///sdcard/Downloads/test.html";

        mDelegate.shouldRequestFileAccess = false;
        // Verify no overrides if file access is allowed (under different load conditions).
        checkUrl(fileUrl).expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl(fileUrl)
                .withPageTransition(PageTransition.RELOAD)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
        checkUrl(fileUrl)
                .withPageTransition(PageTransition.AUTO_TOPLEVEL)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);

        mDelegate.shouldRequestFileAccess = true;
        // Verify that the file intent action is triggered if file access is not allowed.
        checkUrl(fileUrl)
                .withPageTransition(PageTransition.AUTO_TOPLEVEL)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_ASYNC_ACTION, START_FILE);
    }

    @SmallTest
    public void testSms_DispatchIntentToDefaultSmsApp() {
        final String referer = "https://www.google.com/";
        mDelegate.defaultSmsPackageName = TEXT_APP_2_PACKAGE_NAME;

        checkUrl("sms:+012345678?body=hello%20there")
                .withReferrer(referer)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        assertNotNull(mDelegate.startActivityIntent);
        assertEquals(TEXT_APP_2_PACKAGE_NAME, mDelegate.startActivityIntent.getPackage());
    }

    @SmallTest
    public void testSms_DefaultSmsAppDoesNotHandleIntent() {
        final String referer = "https://www.google.com/";
        // Note that this package does not resolve the intent.
        mDelegate.defaultSmsPackageName = "text_app_3";

        checkUrl("sms:+012345678?body=hello%20there")
                .withReferrer(referer)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        assertNotNull(mDelegate.startActivityIntent);
        assertNull(mDelegate.startActivityIntent.getPackage());
    }

    @SmallTest
    public void testSms_DispatchIntentSchemedUrlToDefaultSmsApp() {
        final String referer = "https://www.google.com/";
        mDelegate.defaultSmsPackageName = TEXT_APP_2_PACKAGE_NAME;

        checkUrl("intent://012345678?body=hello%20there/#Intent;scheme=sms;end")
                .withReferrer(referer)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);

        assertNotNull(mDelegate.startActivityIntent);
        assertEquals(TEXT_APP_2_PACKAGE_NAME, mDelegate.startActivityIntent.getPackage());
    }

    /**
     * Test that tapping on a link which is outside of the referrer WebAPK's scope brings the
     * user back to Chrome.
     */
    @SmallTest
    public void testLeaveWebApk_LinkOutOfScope() {
        checkUrl(SEARCH_RESULT_URL_FOR_TOM_HANKS)
                .withWebApkPackageName(WEBAPK_PACKAGE_NAME)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT, START_CHROME);
    }

    /**
     * Test that tapping a link which falls solely into the scope of a WebAPK does not bypass the
     * intent picker if WebAPKs are disabled in the command line.
     */
    @SmallTest
    public void testLaunchWebApk_WebApkDisabledCommandLine() {
        checkUrl(WEBAPK_SCOPE)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    /**
     * Test that tapping a link which falls solely in the scope of a WebAPK launches a WebAPK
     * without showing the intent picker if WebAPKs are enabled in the command line.
     */
    @SmallTest
    public void testLaunchWebApk_BypassIntentPicker() {
        CommandLine.getInstance().appendSwitch(ChromeSwitches.ENABLE_WEBAPK);
        checkUrl(WEBAPK_SCOPE)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT, START_WEBAPK);
    }

    /**
     * Test that tapping a link which falls in the scope of multiple intent handlers, one of which
     * is a WebAPK, shows the intent picker.
     */
    @SmallTest
    public void testLaunchWebApk_ShowIntentPickerMultipleIntentHandlers() {
        CommandLine.getInstance().appendSwitch(ChromeSwitches.ENABLE_WEBAPK);
        checkUrl(WEBAPK_WITH_NATIVE_APP_SCOPE)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    /**
     * Test that tapping a link which falls solely into the scope of a different WebAPK launches a
     * WebAPK without showing the intent picker.
     */
    @SmallTest
    public void testLaunchWebApk_BypassIntentPickerFromAnotherWebApk() {
        CommandLine.getInstance().appendSwitch(ChromeSwitches.ENABLE_WEBAPK);
        checkUrl(WEBAPK_SCOPE)
                .withReferrer(WEBAPK_WITH_NATIVE_APP_SCOPE)
                .withWebApkPackageName(WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT, START_WEBAPK);
    }

    /**
     * Test that a link which falls into the scope of an invalid WebAPK (e.g. it was incorrectly
     * signed) does not get any special WebAPK handling. The first time that the user taps on the
     * link, the intent picker should be shown.
     */
    @SmallTest
    public void testLaunchWebApk_ShowIntentPickerInvalidWebApk() {
        CommandLine.getInstance().appendSwitch(ChromeSwitches.ENABLE_WEBAPK);
        checkUrl(COUNTERFEIT_WEBAPK_SCOPE)
                .expecting(OverrideUrlLoadingResult.OVERRIDE_WITH_EXTERNAL_INTENT,
                        START_OTHER_ACTIVITY);
    }

    /**
     * Test that tapping a link which falls into the scope of the current WebAPK stays within the
     * WebAPK.
     */
    @SmallTest
    public void testLaunchWebApk_StayInSameWebApk() {
        CommandLine.getInstance().appendSwitch(ChromeSwitches.ENABLE_WEBAPK);
        checkUrl(WEBAPK_SCOPE + "/new.html")
                .withReferrer(WEBAPK_SCOPE)
                .withWebApkPackageName(WEBAPK_PACKAGE_NAME)
                .expecting(OverrideUrlLoadingResult.NO_OVERRIDE, IGNORE);
    }

    private static ResolveInfo newResolveInfo(String packageName, String name) {
        ActivityInfo ai = new ActivityInfo();
        ai.packageName = packageName;
        ai.name = name;
        ResolveInfo ri = new ResolveInfo();
        ri.activityInfo = ai;
        return ri;
    }

    private static class TestExternalNavigationDelegate implements ExternalNavigationDelegate {
        private Context mContext;

        public void setContext(Context context) {
            mContext = context;
        }

        @Override
        public List<ResolveInfo> queryIntentActivities(Intent intent) {
            List<ResolveInfo> list = new ArrayList<ResolveInfo>();
            // TODO(yfriedman): We shouldn't have a separate global override just for tests - we
            // should mimic the appropriate intent resolution intead.
            if (mQueryIntentOverride != null) {
                if (mQueryIntentOverride.booleanValue()) {
                    list.add(newResolveInfo("foo", "foo"));
                } else {
                    return list;
                }
            }
            String dataString = intent.getDataString();
            if (dataString.startsWith("http://")
                    || intent.getDataString().startsWith("https://")) {
                list.add(newResolveInfo("chrome", "chrome"));
            }
            if (dataString.startsWith("http://m.youtube.com")
                    || intent.getDataString().startsWith("http://youtube.com")) {
                list.add(newResolveInfo("youtube", "youtube"));
            } else if (dataString.startsWith(PLUS_STREAM_URL)) {
                list.add(newResolveInfo("plus", "plus"));
            } else if (intent.getDataString().startsWith(CALENDAR_URL)) {
                list.add(newResolveInfo("calendar", "calendar"));
            } else if (dataString.startsWith("sms")) {
                list.add(newResolveInfo(
                        TEXT_APP_1_PACKAGE_NAME, TEXT_APP_1_PACKAGE_NAME + ".cls"));
                list.add(newResolveInfo(
                        TEXT_APP_2_PACKAGE_NAME, TEXT_APP_2_PACKAGE_NAME + ".cls"));
            } else if (dataString.startsWith(WEBAPK_SCOPE)) {
                list.add(newResolveInfo(WEBAPK_PACKAGE_NAME, WEBAPK_PACKAGE_NAME));
            } else if (dataString.startsWith(WEBAPK_WITH_NATIVE_APP_SCOPE)) {
                list.add(newResolveInfo(WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME,
                        WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME));
                list.add(newResolveInfo(NATIVE_APP_PACKAGE_NAME, NATIVE_APP_PACKAGE_NAME));
            } else if (dataString.startsWith(COUNTERFEIT_WEBAPK_SCOPE)) {
                list.add(newResolveInfo(COUNTERFEIT_WEBAPK_PACKAGE_NAME, COUNTERFEIT_WEBAPK_SCOPE));
            } else {
                list.add(newResolveInfo("foo", "foo"));
            }
            return list;
        }

        @Override
        public boolean willChromeHandleIntent(Intent intent) {
            return !isSpecializedHandlerAvailable(queryIntentActivities(intent));
        }

        @Override
        public boolean isSpecializedHandlerAvailable(List<ResolveInfo> resolveInfos) {
            return countSpecializedHandlers(resolveInfos) > 0;
        }

        @Override
        public int countSpecializedHandlers(List<ResolveInfo> resolveInfos) {
            return getSpecializedHandlers(resolveInfos).size();
        }

        private ArrayList<String> getSpecializedHandlers(List<ResolveInfo> infos) {
            ArrayList<String> result = new ArrayList<>();
            if (infos == null) {
                return result;
            }
            int count = 0;
            for (ResolveInfo info : infos) {
                String packageName = info.activityInfo.packageName;
                if (packageName.equals("youtube") || packageName.equals("calendar")
                        || packageName.equals(COUNTERFEIT_WEBAPK_PACKAGE_NAME)
                        || packageName.equals(NATIVE_APP_PACKAGE_NAME)
                        || packageName.equals(WEBAPK_PACKAGE_NAME)
                        || packageName.equals(WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME)) {
                    result.add(packageName);
                }
            }
            return result;
        }

        @Override
        public String findWebApkPackageName(List<ResolveInfo> infos) {
            if (infos == null) {
                return null;
            }
            for (ResolveInfo info : infos) {
                String packageName = info.activityInfo.packageName;
                if (packageName.equals(WEBAPK_PACKAGE_NAME)
                        || packageName.equals(WEBAPK_WITH_NATIVE_APP_PACKAGE_NAME)) {
                    return packageName;
                }
            }
            return null;
        }

        @Override
        public String getPackageName() {
            return "test";
        }

        @Override
        public void startActivity(Intent intent) {
            startActivityIntent = intent;
        }

        @Override
        public boolean startActivityIfNeeded(Intent intent) {
            // For simplicity, don't distinguish between startActivityIfNeeded and startActivity
            // until a test requires this distinction.
            startActivityIntent = intent;
            return true;
        }

        @Override
        public void startIncognitoIntent(Intent intent, String referrerUrl, String fallbackUrl,
                Tab tab, boolean needsToCloseTab) {
            startIncognitoIntentCalled = true;
        }

        @Override
        public boolean shouldRequestFileAccess(String url, Tab tab) {
            return shouldRequestFileAccess;
        }

        @Override
        public void startFileIntent(Intent intent, String referrerUrl, Tab tab,
                boolean needsToCloseTab) {
            startFileIntentCalled = true;
        }

        @Override
        public OverrideUrlLoadingResult clobberCurrentTab(
                String url, String referrerUrl, Tab tab) {
            mNewUrlAfterClobbering = url;
            mReferrerUrlForClobbering = referrerUrl;
            return OverrideUrlLoadingResult.OVERRIDE_WITH_CLOBBERING_TAB;
        }

        @Override
        public void maybeSetWindowId(Intent intent) {
        }

        @Override
        public void maybeRecordAppHandlersInIntent(Intent intent, List<ResolveInfo> info) {
        }

        @Override
        public boolean isChromeAppInForeground() {
            return mIsChromeAppInForeground;
        }

        @Override
        public boolean isDocumentMode() {
            return FeatureUtilities.isDocumentMode(mContext);
        }

        @Override
        public String getDefaultSmsPackageName() {
            return defaultSmsPackageName;
        }

        @Override
        public boolean isPdfDownload(String url) {
            return false;
        }

        public void reset() {
            startActivityIntent = null;
            startIncognitoIntentCalled = false;
            startFileIntentCalled = false;
        }

        public void setCanResolveActivity(boolean value) {
            mQueryIntentOverride = value;
        }

        public String getNewUrlAfterClobbering() {
            return mNewUrlAfterClobbering;
        }

        public String getReferrerUrlForClobbering() {
            return mReferrerUrlForClobbering;
        }

        public void setIsChromeAppInForeground(boolean value) {
            mIsChromeAppInForeground = value;
        }

        public Intent startActivityIntent = null;
        public boolean startIncognitoIntentCalled = false;

        // This should not be reset for every run of check().
        private Boolean mQueryIntentOverride;

        private String mNewUrlAfterClobbering;
        private String mReferrerUrlForClobbering;
        public boolean mIsChromeAppInForeground = true;

        public boolean shouldRequestFileAccess;
        public boolean startFileIntentCalled;
        public String defaultSmsPackageName;
    }

    private void checkIntentSanity(Intent intent, String name) {
        assertTrue("The invoked " + name + " doesn't have the BROWSABLE category set\n",
                intent.hasCategory(Intent.CATEGORY_BROWSABLE));
        assertNull("The invoked " + name + " should not have a Component set\n",
                intent.getComponent());
    }

    private ExternalNavigationTestParams checkUrl(String url) {
        return new ExternalNavigationTestParams(url);
    }

    private class ExternalNavigationTestParams {
        private final String mUrl;

        private String mReferrerUrl;
        private boolean mIsIncognito;
        private int mPageTransition = PageTransition.LINK;
        private boolean mIsRedirect;
        private boolean mChromeAppInForegroundRequired = true;
        private boolean mIsBackgroundTabNavigation;
        private String mWebApkPackageName;
        private boolean mHasUserGesture;
        private TabRedirectHandler mRedirectHandler;

        private ExternalNavigationTestParams(String url) {
            mUrl = url;
        }

        public ExternalNavigationTestParams withWebApkPackageName(String webApkPackageName) {
            mWebApkPackageName = webApkPackageName;
            return this;
        }

        public ExternalNavigationTestParams withReferrer(String referrerUrl) {
            mReferrerUrl = referrerUrl;
            return this;
        }

        public ExternalNavigationTestParams withIsIncognito(boolean isIncognito) {
            mIsIncognito = isIncognito;
            return this;
        }

        public ExternalNavigationTestParams withPageTransition(int pageTransition) {
            mPageTransition = pageTransition;
            return this;
        }

        public ExternalNavigationTestParams withIsRedirect(boolean isRedirect) {
            mIsRedirect = isRedirect;
            return this;
        }

        public ExternalNavigationTestParams withChromeAppInForegroundRequired(
                boolean foregroundRequired) {
            mChromeAppInForegroundRequired = foregroundRequired;
            return this;
        }

        public ExternalNavigationTestParams withIsBackgroundTabNavigation(
                boolean isBackgroundTabNavigation) {
            mIsBackgroundTabNavigation = isBackgroundTabNavigation;
            return this;
        }

        public ExternalNavigationTestParams withHasUserGesture(boolean hasUserGesture) {
            mHasUserGesture = hasUserGesture;
            return this;
        }

        public ExternalNavigationTestParams withRedirectHandler(TabRedirectHandler handler) {
            mRedirectHandler = handler;
            return this;
        }

        public void expecting(OverrideUrlLoadingResult expectedOverrideResult,
                int otherExpectation) {
            boolean expectStartIncognito = (otherExpectation & START_INCOGNITO) != 0;
            boolean expectStartActivity =
                    (otherExpectation & (START_CHROME | START_WEBAPK | START_OTHER_ACTIVITY)) != 0;
            boolean expectStartChrome = (otherExpectation & START_CHROME) != 0;
            boolean expectStartWebApk = (otherExpectation & START_WEBAPK) != 0;
            boolean expectStartOtherActivity = (otherExpectation & START_OTHER_ACTIVITY) != 0;
            boolean expectStartFile = (otherExpectation & START_FILE) != 0;
            boolean expectSaneIntent = expectStartOtherActivity
                    && (otherExpectation & INTENT_SANITIZATION_EXCEPTION) == 0;

            mDelegate.reset();

            ExternalNavigationParams params = new ExternalNavigationParams.Builder(
                    mUrl, mIsIncognito, mReferrerUrl,
                    mPageTransition, mIsRedirect)
                    .setApplicationMustBeInForeground(mChromeAppInForegroundRequired)
                    .setRedirectHandler(mRedirectHandler)
                    .setIsBackgroundTabNavigation(mIsBackgroundTabNavigation)
                    .setIsMainFrame(true)
                    .setWebApkPackageName(mWebApkPackageName)
                    .setHasUserGesture(mHasUserGesture)
                    .build();
            OverrideUrlLoadingResult result = mUrlHandler.shouldOverrideUrlLoading(params);
            boolean startActivityCalled = false;
            boolean startChromeCalled = false;
            boolean startWebApkCalled = false;
            if (mDelegate.startActivityIntent != null) {
                startActivityCalled = true;
                String packageName = mDelegate.startActivityIntent.getPackage();
                if (packageName != null) {
                    startChromeCalled = packageName.equals(mDelegate.getPackageName());
                    startWebApkCalled =
                            packageName.startsWith(WebApkConstants.WEBAPK_PACKAGE_PREFIX);
                }
            }

            assertEquals(expectedOverrideResult, result);
            assertEquals(expectStartIncognito, mDelegate.startIncognitoIntentCalled);
            assertEquals(expectStartActivity, startActivityCalled);
            assertEquals(expectStartChrome, startChromeCalled);
            assertEquals(expectStartWebApk, startWebApkCalled);
            assertEquals(expectStartFile, mDelegate.startFileIntentCalled);

            if (startActivityCalled && expectSaneIntent) {
                checkIntentSanity(mDelegate.startActivityIntent, "Intent");
                if (mDelegate.startActivityIntent.getSelector() != null) {
                    checkIntentSanity(mDelegate.startActivityIntent.getSelector(),
                            "Intent's selector");
                }
            }
        }
    }

    private static class TestPackageManager extends MockPackageManager {
        @Override
        public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
            List<ResolveInfo> resolves = new ArrayList<ResolveInfo>();
            if (intent.getDataString().startsWith("market:")) {
                resolves.add(newResolveInfo("market", "market"));
            } else if (intent.getDataString().startsWith("http://m.youtube.com")
                    ||  intent.getDataString().startsWith("http://youtube.com")) {
                resolves.add(newResolveInfo("youtube", "youtube"));
            } else {
                resolves.add(newResolveInfo("foo", "foo"));
            }
            return resolves;
        }
    }

    private static class TestContext extends MockContext {
        @Override
        public PackageManager getPackageManager() {
            return new TestPackageManager();
        }

        @Override
        public String getPackageName() {
            return "test.app.name";
        }

    }
}
