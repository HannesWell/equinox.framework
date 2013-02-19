/*******************************************************************************
 * Copyright (c) 2004, 2012 IBM Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Contributors:
 *     IBM Corporation - initial API and implementation
 *******************************************************************************/
package org.eclipse.osgi.storage;

import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.util.*;
import org.eclipse.osgi.container.*;
import org.eclipse.osgi.framework.util.Headers;
import org.eclipse.osgi.storage.BundleInfo.Generation;
import org.osgi.framework.Constants;
import org.osgi.framework.namespace.HostNamespace;
import org.osgi.framework.wiring.BundleRevision;

/**
 * This class is used to localize manifest headers for a revision.
 */
public class ManifestLocalization {
	final String defaultRoot;
	private final Generation generation;
	private final Dictionary<String, String> rawHeaders;
	private volatile Dictionary<String, String> defaultLocaleHeaders = null;
	private final Hashtable<String, BundleResourceBundle> cache = new Hashtable<String, BundleResourceBundle>(5);

	public ManifestLocalization(Generation generation, Dictionary<String, String> rawHeaders, String defaultRoot) {
		this.generation = generation;
		this.rawHeaders = rawHeaders;
		this.defaultRoot = defaultRoot;
	}

	public void clearCache() {
		synchronized (cache) {
			cache.clear();
			defaultLocaleHeaders = null;
		}
	}

	Dictionary<String, String> getHeaders(String localeString) {
		if (localeString == null)
			localeString = Locale.getDefault().toString();
		if (localeString.length() == 0)
			return rawHeaders;
		boolean isDefaultLocale = localeString.equals(Locale.getDefault().toString());
		Dictionary<String, String> currentDefault = defaultLocaleHeaders;
		if (isDefaultLocale && currentDefault != null) {
			return currentDefault;
		}
		if (generation.getRevision().getRevisions().getModule().getState().equals(Module.State.UNINSTALLED)) {
			// defaultLocaleHeaders should have been initialized on uninstall
			if (currentDefault != null)
				return currentDefault;
			return rawHeaders;
		}
		ResourceBundle localeProperties = getResourceBundle(localeString, isDefaultLocale);
		Enumeration<String> eKeys = this.rawHeaders.keys();
		Headers<String, String> localeHeaders = new Headers<String, String>(this.rawHeaders.size());
		while (eKeys.hasMoreElements()) {
			String key = eKeys.nextElement();
			String value = this.rawHeaders.get(key);
			if (value.startsWith("%") && (value.length() > 1)) { //$NON-NLS-1$
				String propertiesKey = value.substring(1);
				try {
					value = localeProperties == null ? propertiesKey : (String) localeProperties.getObject(propertiesKey);
				} catch (MissingResourceException ex) {
					value = propertiesKey;
				}
			}
			localeHeaders.set(key, value);
		}
		localeHeaders.setReadOnly();
		if (isDefaultLocale) {
			defaultLocaleHeaders = localeHeaders;
		}
		return (localeHeaders);
	}

	private String[] buildNLVariants(String nl) {
		List<String> result = new ArrayList<String>();
		while (nl.length() > 0) {
			result.add(nl);
			int i = nl.lastIndexOf('_');
			nl = (i < 0) ? "" : nl.substring(0, i); //$NON-NLS-1$
		}
		result.add(""); //$NON-NLS-1$
		return result.toArray(new String[result.size()]);
	}

	/*
	 * This method find the appropriate Manifest Localization file inside the
	 * bundle. If not found, return null.
	 */
	ResourceBundle getResourceBundle(String localeString, boolean isDefaultLocale) {
		BundleResourceBundle resourceBundle = lookupResourceBundle(localeString);
		if (isDefaultLocale)
			return (ResourceBundle) resourceBundle;
		// need to determine if this is resource bundle is an empty stem
		// if it is then the default locale should be used
		if (resourceBundle == null || resourceBundle.isStemEmpty())
			return (ResourceBundle) lookupResourceBundle(Locale.getDefault().toString());
		return (ResourceBundle) resourceBundle;
	}

	private BundleResourceBundle lookupResourceBundle(String localeString) {
		// get the localization header as late as possible to avoid accessing the raw headers
		// getting the first value from the raw headers forces the manifest to be parsed (bug 332039)
		String localizationHeader = rawHeaders.get(Constants.BUNDLE_LOCALIZATION);
		if (localizationHeader == null)
			localizationHeader = Constants.BUNDLE_LOCALIZATION_DEFAULT_BASENAME;
		synchronized (cache) {
			BundleResourceBundle result = cache.get(localeString);
			if (result != null)
				return result.isEmpty() ? null : result;
			String[] nlVarients = buildNLVariants(localeString);
			BundleResourceBundle parent = null;
			for (int i = nlVarients.length - 1; i >= 0; i--) {
				BundleResourceBundle varientBundle = null;
				URL varientURL = findResource(localizationHeader + (nlVarients[i].equals("") ? nlVarients[i] : '_' + nlVarients[i]) + ".properties"); //$NON-NLS-1$ //$NON-NLS-2$
				if (varientURL == null) {
					varientBundle = cache.get(nlVarients[i]);
				} else {
					InputStream resourceStream = null;
					try {
						resourceStream = varientURL.openStream();
						varientBundle = new LocalizationResourceBundle(resourceStream);
					} catch (IOException e) {
						// ignore and continue
					} finally {
						if (resourceStream != null) {
							try {
								resourceStream.close();
							} catch (IOException e3) {
								//Ignore exception
							}
						}
					}
				}

				if (varientBundle == null) {
					varientBundle = new EmptyResouceBundle(nlVarients[i]);
				}
				if (parent != null)
					varientBundle.setParent((ResourceBundle) parent);
				cache.put(nlVarients[i], varientBundle);
				parent = varientBundle;
			}
			result = cache.get(localeString);
			return result.isEmpty() ? null : result;
		}
	}

	private URL findResource(String resource) {
		ModuleWiring searchWiring = generation.getRevision().getWiring();
		if (searchWiring != null) {
			if ((generation.getRevision().getTypes() & BundleRevision.TYPE_FRAGMENT) != 0) {
				List<ModuleWire> hostWires = searchWiring.getRequiredModuleWires(HostNamespace.HOST_NAMESPACE);
				searchWiring = null;
				Long lowestHost = Long.MAX_VALUE;
				if (hostWires != null) {
					// search for the host with the highest ID
					for (ModuleWire hostWire : hostWires) {
						Long hostID = hostWire.getProvider().getRevisions().getModule().getId();
						if (hostID.compareTo(lowestHost) <= 0) {
							lowestHost = hostID;
							searchWiring = hostWire.getProviderWiring();
						}
					}
				}
			}
		}
		if (searchWiring != null) {
			int lastSlash = resource.lastIndexOf('/');
			String path = lastSlash > 0 ? resource.substring(0, lastSlash) : "/"; //$NON-NLS-1$
			String fileName = lastSlash != -1 ? resource.substring(lastSlash + 1) : resource;
			List<URL> result = searchWiring.findEntries(path, fileName, 0);
			return (result == null || result.isEmpty()) ? null : result.get(0);
		}
		// search the raw bundle file for the generation
		return generation.getEntry(resource);
	}

	private interface BundleResourceBundle {
		void setParent(ResourceBundle parent);

		boolean isEmpty();

		boolean isStemEmpty();
	}

	private class LocalizationResourceBundle extends PropertyResourceBundle implements BundleResourceBundle {
		public LocalizationResourceBundle(InputStream in) throws IOException {
			super(in);
		}

		public void setParent(ResourceBundle parent) {
			super.setParent(parent);
		}

		public boolean isEmpty() {
			return false;
		}

		public boolean isStemEmpty() {
			return parent == null;
		}
	}

	class EmptyResouceBundle extends ResourceBundle implements BundleResourceBundle {
		private final String localeString;

		public EmptyResouceBundle(String locale) {
			super();
			this.localeString = locale;
		}

		@SuppressWarnings("unchecked")
		public Enumeration<String> getKeys() {
			return Collections.enumeration(Collections.EMPTY_LIST);
		}

		protected Object handleGetObject(String arg0) throws MissingResourceException {
			return null;
		}

		public void setParent(ResourceBundle parent) {
			super.setParent(parent);
		}

		public boolean isEmpty() {
			if (parent == null)
				return true;
			return ((BundleResourceBundle) parent).isEmpty();
		}

		public boolean isStemEmpty() {
			if (defaultRoot.equals(localeString))
				return false;
			if (parent == null)
				return true;
			return ((BundleResourceBundle) parent).isStemEmpty();
		}
	}
}