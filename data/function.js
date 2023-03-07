var xmlHttp = createXmlHttpObject();
function createXmlHttpObject() {
    if (window.XMLHttpRequest) {
        xmlHttp = new XMLHttpRequest();
    } else {
        xmlHttp = new ActiveXObject('Microsoft.XMLHTTP');
    }
    return xmlHttp;
}

function load() {
    if (xmlHttp.readyState == 0 || xmlHttp.readyState == 4) {
        xmlHttp.open('GET', '/config', true);
        xmlHttp.send(null);
        xmlHttp.onload = function () {
            jsonResponse = JSON.parse(xmlHttp.responseText);
            loadBlock();
        }
    }
}

function loadBlock() {
    newData = JSON.parse(xmlHttp.responseText);
    data = document.getElementsByTagName('body')[0].innerHTML;
    var newString;
    for (var key in newData) {
        newString = data.replace(new RegExp('{{' + key + '}}', 'g'), newData[key]);
        data = newString;
    }
    document.getElementsByTagName('body')[0].innerHTML = newString;
    setFirmvareValue('version', 'firmware');
    setGpioValue('workModeSelect', 'workMode');
    handleServerResponse();
}

function getValue(id) {
    var value = document.getElementById(id).value;
    return value;
}

function getSelectValue(id) {
    var select = document.getElementById(id);
    var value = select.value;
    return value;
}

function sendRequest(submit, server) {
    request = new XMLHttpRequest();
    request.open("GET", server, true);
    request.send();
}

function saveSetting(submit) {
    server = "/setting?ssid=" + getValue('ssid') + "&password=" + encodeURIComponent(getValue('password'))
        + "&mqttHostName=" + getValue('mqttHostName') + "&mqttHostPort=" + getValue('mqttHostPort')
        + "&mqttUserLogin=" + getValue('mqttUserLogin') + "&mqttUserPassword=" + encodeURIComponent(getValue('mqttUserPassword'))
        + "&topicPrefix=" + getValue('topicPrefix')
        + "&deviceName=" + getValue('deviceName')
        + "&espnowNetName=" + getValue('espnowNetName')
        + "&workMode=" + getSelectValue('workModeSelect')
        + "&ntpHostName=" + getValue('ntpHostName')
        + "&gmtOffset=" + getValue('gmtOffset');
    sendRequest(submit, server);
    alert("Please restart device for changes apply.");
}

function restart(submit) {
    server = "/restart";
    sendRequest(submit, server);
}

function setFirmvareValue(id, value) {
    document.getElementById(id).innerHTML = document.getElementById(value).value;
}

function setGpioValue(id, value) {
    var select = document.getElementById(id);
    select.value = document.getElementById(value).value;
}