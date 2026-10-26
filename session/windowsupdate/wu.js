/* The Windows Update site's engine for the Linux system.

   The site (version 4, as the Internet Archive kept it) ran its scripts,
   top.js and top2.js, over Microsoft's ActiveX control (IUCtl) and its
   servers: the control scanned the computer against the server's catalog,
   and the server drew the pages of the updates found (results.asp), from
   what the scripts posted to it. This file comes after them in default.hta
   and replaces those two: the scan, the installation and the history are
   session/xp_update.py's, from the system's package manager, and the pages
   of updates are drawn here, in results.htm, by the site's own styles.

   Wine's WScript.Shell cannot read a program's output: the helper runs
   through linux-launch.exe, and writes what it finds in
   C:\xp\windowsupdate\state, which this polls. */

var g_oShell = new ActiveXObject("WScript.Shell");
var g_oFiles = new ActiveXObject("Scripting.FileSystemObject");
var conStateFolder = "C:\\xp\\windowsupdate\\state\\";
var conLinuxProvider = "linux";
var conLinuxProduct = "linux.system";
var conFlatpakProduct = "linux.flatpak";

var g_iUpdatesLen = 0;              // the updates in the basket (content.js)
var g_sPostedXML, g_sPostedURL;     // what fnPostData would have posted
var g_oScanState, g_oHistoryState, g_oInstallState, g_iWaitTimer;

/* helper */

function fnReadState(sName){
	var sPath, oStream, sText;

	sPath = conStateFolder + sName + ".json";
	if(!g_oFiles.FileExists(sPath)) return null;
	try{
		oStream = g_oFiles.OpenTextFile(sPath, 1);
		sText = oStream.ReadAll();
		oStream.Close();
		return eval("(" + sText + ")"); // written by xp_update.py
	}catch(e){
		return null;
	}
}

function fnRunHelper(sCommand){
	var sPath;

	sPath = conStateFolder + sCommand + ".json";
	try{
		if(g_oFiles.FileExists(sPath)) g_oFiles.DeleteFile(sPath, true);
	}catch(e){}
	g_oShell.Run("\"C:\\xp\\bin\\linux-launch.exe\" python3 \"" + WU_HELPER + "\" " + sCommand + " --prefix \"" + WU_PREFIX + "\"", 0, false);
}

// fnDone(state) once the helper is done, fnProgress(state) while it works
function fnWaitForHelper(sName, fnDone, fnProgress){
	var oState;

	window.clearTimeout(g_iWaitTimer);
	oState = fnReadState(sName);
	if(oState != null && oState.status != "working"){
		fnDone(oState);
		return;
	}
	if(oState != null && fnProgress != null) fnProgress(oState);
	g_iWaitTimer = window.setTimeout(function(){ fnWaitForHelper(sName, fnDone, fnProgress); }, 400);
}

/* control init */

function fnInitializeControl(bAttemptedInstall){
	g_oControl = {};
	return fnTestControl();
}

function fnTestControl(){
	g_bControlInitialized = true;
	eTOC.fnEnableTOC();
	// Windows Update Catalog is for the computers of a network
	eTOC.eCatalogLink.style.display = "none";
	fnCreateCatalogXML();
	return true;
}

function control_GetSystemSpec(sXmlClasses){
	return "<systemInfo><computerSystem administrator=\"1\" autoUpdateEnabled=\"0\" /><platform name=\"VER_PLATFORM_WIN32_NT\"><productType>VER_NT_WORKSTATION</productType></platform><locale context=\"OS\"><language>" + conLangCode + "</language></locale><locale context=\"USER\"><language>" + conLangCode + "</language></locale></systemInfo>";
}

function control_SetOperationMode(sUuidOperationID, lCommand){
	return true;
}

function control_RebootMachine(){
	g_oShell.Run("\"C:\\xp\\bin\\linux-launch.exe\" systemctl reboot", 0, false);
}

// history.js's installation history, from the package manager's log
function control_GetHistory(sDateTimeFrom, sDateTimeTo, sClient, sPath){
	var sXML, aEntries, oEntry, i;

	if(sClient == "GetHistoricalSpeed") return "0";
	if(g_oHistoryState == null || sClient != "") return "";
	aEntries = g_oHistoryState.entries;
	if(aEntries.length == 0) return "";
	sXML = "<items>";
	for(i = 0; i < aEntries.length; i++){
		oEntry = aEntries[i];
		sXML += "<itemStatus timestamp=\"" + oEntry.date.replace(" ", "T") + ":00\"><client>" + fnXMLText(oEntry.source) + "</client>";
		sXML += "<installStatus value=\"COMPLETE\" /><description hidden=\"0\"><descriptionText><title>" + fnXMLText(oEntry.title) + "</title></descriptionText></description></itemStatus>";
	}
	return sXML + "</items>";
}

/* pages */

function fnDisplaySplashPage(iPage){
	g_iSplashPage = iPage;
	eContent.location.replace("splash" + iPage + ".htm");
}

function fnDisplayErrorPage(iError, bDisableTOC, sErrorDetails){
	try{
		if(bDisableTOC) eTOC.fnDisableTOC(iError, sErrorDetails);
		eContent.location.replace("error.htm");
	}catch(e){}
}

function fnDisplayHistory(bCorporate){
	g_bCorporate = false;
	fnRunHelper("history");
	fnWaitForHelper("history", function(oState){
		g_oHistoryState = oState;
		eContent.location.href = "history.htm";
	});
}

// results.asp, status.asp and personalize.asp are drawn here from what the
// site posted to the server
function fnPostData(sData, sURL){
	var sPage;

	g_sPostedXML = sData;
	g_sPostedURL = sURL;
	g_bPosted = true;
	sPage = sURL.replace(/\?.*$/, "").replace(/^.*\//, "").replace(/\.asp$/, ".htm");
	eContent.location.replace(sPage);
}

fnUpdateTOCBasket = (function(fnSiteUpdateTOCBasket){
	return function(){
		g_iUpdatesLen = g_iConsumerBasketCount;
		return fnSiteUpdateTOCBasket();
	};
})(fnUpdateTOCBasket);

/* detection */

function fnInitDetect(){
	g_sRegionalSettings = fnGetRegionalSettings().join("|@|");
	g_iConnectionSpeed = 0;
	fnRunHelper("scan");
	fnWaitForHelper("scan", fnEndScan, fnShowScanProgress);
}

function fnShowScanProgress(oState){
	var iPercentComplete;

	iPercentComplete = oState.progress || 0;
	eTOC.eAvailableUpdates.children[1].innerText = L_ScanForAvailableUpdates_Text + " ... " + iPercentComplete + "% ";
	if("object" == typeof(eContent.ePercentComplete)) eContent.ePercentComplete.innerText = iPercentComplete + "% " + L_Complete_Text;
}

function fnEndScan(oState){
	var oProvider, oProduct, oFlatpak, aUpdates, oUpdate, iFlatpakLen, i;

	g_oScanState = oState;
	if(oState.status == "error"){
		g_bScanning = false;
		fnEndTOCDetectUpdates(0, 0);
		fnDisplayErrorPage(conErrorNetwork, false, oState.error);
		return false;
	}

	oProvider = fnNewCatalogNode(g_oCatalogXML, "provider", conLinuxProvider, oState.system);
	oProduct = fnNewCatalogNode(oProvider, "product", conLinuxProduct, oState.system);
	fnNewElement(oProduct, "category", conCategoryOS);
	oFlatpak = null;
	iFlatpakLen = 0;

	aUpdates = oState.updates;
	for(i = 0; i < aUpdates.length; i++){
		oUpdate = aUpdates[i];
		if(oUpdate.category == "drivers") continue;
		if(oUpdate.kind == "flatpak"){
			if(oFlatpak == null){
				oFlatpak = fnNewCatalogNode(oProvider, "product", conFlatpakProduct, oState.applications);
				fnNewElement(oFlatpak, "category", conCategorySoftware);
			}
			fnAddUpdateNode(oFlatpak, oUpdate, oState.applications);
			iFlatpakLen++;
		}else{
			fnAddUpdateNode(oProduct, oUpdate, oUpdate.repository);
		}
	}

	fnAddTOCLink(conLinuxProduct, oState.system, conCategoryOS, oProduct.selectNodes("group/item[priority > '3']").length);
	if(oFlatpak != null) fnAddTOCLink(conFlatpakProduct, oState.applications, conCategorySoftware, iFlatpakLen);

	fnEndDetectUpdates(true);
	fnAddLinuxUpdatesToBasket();
}

// top2.js's, with the drivers of the scan
function fnDetectDriverUpdates(){
	var oProvider, oProduct, aUpdates, oUpdate, oItem, i;

	oProvider = null;
	aUpdates = g_oScanState.updates;
	for(i = 0; i < aUpdates.length; i++){
		oUpdate = aUpdates[i];
		if(oUpdate.category != "drivers") continue;
		if(oProvider == null){
			oProvider = fnNewCatalogNode(g_oCatalogXML, "provider", "devices", "");
			oProduct = fnNewCatalogNode(oProvider, "product", "devices.linux", "");
		}
		oItem = fnAddUpdateNode(oProduct, oUpdate, oUpdate.repository);
		oItem.setAttribute("driver", "1");
	}
}

// The system's updates install together (Arch Linux upgrades the whole
// system at once): they are all selected, as critical updates are.
function fnAddLinuxUpdatesToBasket(){
	var vUpdates, i;

	vUpdates = g_oCatalogXML.selectNodes("provider/product/group/item[not(@basket)]");
	for(i = 0; i < vUpdates.length; i++){
		vUpdates[i].setAttribute("basket", "1");
		g_iConsumerBasketCount++;
		g_iConsumerBasketSize += parseInt(vUpdates[i].selectSingleNode("size").text);
	}
	fnUpdateTOCBasket();
}

function fnAddUpdateNode(oProduct, oUpdate, sGroup){
	var oGroup, oItem, sGroupID;

	sGroupID = oProduct.selectSingleNode("identity/@itemID").text + "." + sGroup;
	oGroup = oProduct.selectSingleNode("group[identity/@itemID = '" + sGroupID + "']");
	if(oGroup == null){
		oGroup = fnNewCatalogNode(oProduct, "group", sGroupID, sGroup);
		fnNewElement(oGroup, "title", sGroup);
	}
	oItem = fnNewCatalogNode(oGroup, "item", "u_" + oUpdate.kind + "_" + oUpdate.name.replace(/[^A-Za-z0-9_]/g, "_"), oUpdate.name);
	fnNewElement(oItem, "title", oUpdate.name + " " + oUpdate.version);
	fnNewElement(oItem, "size", oUpdate.size);
	fnNewElement(oItem, "priority", (oUpdate.category == "critical") ? "1" : "4");
	fnNewElement(oItem, "description", oUpdate.description);
	if(oUpdate.old != "") fnNewElement(oItem, "installedVersion", oUpdate.old);
	return oItem;
}

function fnNewCatalogNode(oParent, sName, sID, sTitle){
	var oNode, oIdentity;

	oNode = CatalogXML.createElement(sName);
	oIdentity = CatalogXML.createElement("identity");
	oIdentity.setAttribute("itemID", sID);
	oIdentity.setAttribute("name", sTitle);
	oNode.appendChild(oIdentity);
	if(sName == "provider" || sName == "product") fnNewElement(oNode, "title", sTitle);
	oParent.appendChild(oNode);
	return oNode;
}

function fnNewElement(oParent, sName, sText){
	var oNode;

	oNode = CatalogXML.createElement(sName);
	oNode.text = String(sText);
	oParent.appendChild(oNode);
	return oNode;
}

function fnXMLText(s){
	return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

/* installation */

function fnInstall(){
	var iAllLen, iBasketLen;

	if(fnContentDisabled()) return false;

	iAllLen = g_oCatalogXML.selectNodes("provider/product/group/item").length;
	iBasketLen = g_oCatalogXML.selectNodes("provider/product/group/item[@basket]").length;
	if(iBasketLen == 0) return false;
	if(iBasketLen < iAllLen && !window.confirm(L_InstallAllText_Text.replace(/\|UPDATES\|/g, iAllLen))) return false;

	g_bInstalling = true;
	fnRunHelper("install");
	fnDisplaySplashPage(conSplashInstallingWait);
	fnWaitForHelper("install", fnEndInstall);
}

function fnEndInstall(oState){
	var vUpdates, oUpdate, bInstalled, sXML, i;

	g_oInstallState = oState;
	g_bInstalling = false;
	bInstalled = (oState.exit == 0);

	vUpdates = g_oCatalogXML.selectNodes("provider/product/group/item");
	sXML = "<catalog>\n";
	for(i = 0; i < vUpdates.length; i++){
		oUpdate = vUpdates[i];
		sXML += "<item" + (bInstalled ? " installed='1'" : " errorCode='" + oState.exit + "'") + "><title>" + fnXMLText(oUpdate.selectSingleNode("title").text) + "</title></item>";
		if(bInstalled){
			g_bNeedsReboot = g_bNeedsReboot || /^(linux(-[a-z0-9]+)*|.*-ucode|systemd|glibc|nvidia.*) /.test(oUpdate.selectSingleNode("title").text);
			oUpdate.parentNode.removeChild(oUpdate);
		}
	}
	sXML += "</catalog>";

	if(bInstalled){
		g_iConsumerBasketCount = 0;
		g_iConsumerBasketSize = 0;
	}
	fnUpdateTOCNumbers();
	fnPostData(sXML, conConsumerURL + "status.asp?" + conQueryString);
	if(bInstalled && g_bNeedsReboot) window.setTimeout("fnConfirmReboot()", 1000);
}
