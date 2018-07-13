#include "listener.h"
#include <Windows.h>

int main(int argc, char ** argv)
{
	unsigned short usListenPort = 8118;
	unsigned short usPubPort = 8200;
	char szCfgFile[256] = { 0 };
	if (argc > 1) {
		printf("exe [config_file]\n");
		strcpy_s(szCfgFile, sizeof(szCfgFile), argv[1]);
	}
	else {
		char szAppName[256] = { 0 };
		GetModuleFileNameA(GetModuleHandle(NULL), szAppName, sizeof(szAppName));
		char szDrive[32] = { 0 };
		char szDir[256] = { 0 };
		_splitpath_s(szAppName, szDrive, 32, szDir, 256, NULL, 0, NULL, 0);
		sprintf_s(szCfgFile, sizeof(szCfgFile), "%s%snet_config.ini", szDrive, szDir);
	}
	char szLogPath[256] = { 0 };
	usListenPort = (unsigned short)GetPrivateProfileIntA("config", "listen_port", 0, szCfgFile);
	usPubPort = (unsigned short)GetPrivateProfileIntA("config", "publish_port", 0, szCfgFile);
	GetPrivateProfileStringA("config", "log_path", "", szLogPath, sizeof(szLogPath), szCfgFile);

	char szZkHome[256] = { 0 };
	GetPrivateProfileStringA("config", "zookeeper_home", "", szZkHome, sizeof(szZkHome), szCfgFile);

	unsigned int uiUseZk = GetPrivateProfileInt("config", "use_zookeeper", 0, szCfgFile);

	uwb_network::Listener listener(szLogPath);
	if (listener.Start(usListenPort, usPubPort) == 0) {
		printf("start\n");
		getchar();
		listener.Stop();
		printf("stop\n");
	}
	return 0;
}