#ifndef  PCAP_SAVE_H
#define  PCAP_SAVE_H


#include <stdio.h>
#include <string>
#include <iostream>
#include <fstream>
#include <pthread.h>

using namespace std;

#pragma pack(1)

    struct pcap_file_header
    {
        unsigned int  magic;
        unsigned short  version_major;
        unsigned short  version_minor;
        int thiszone;   /* gmt to local correction */
        unsigned int  sigfigs;    /* accuracy of timestamps */
        unsigned int  snaplen;    /* max length saved portion of each pkt */
        unsigned int  linktype;   /* data link type (LINKTYPE_*) */
    };

    struct pcap_pkthdr
    {
        unsigned int tsSec;  /* time stamp */
        unsigned int tsMs;   /* time stamp */
        unsigned int caplen; /* length of portion present */
        unsigned int len;    /* length this packet (off wire) */
    };


#pragma pack()


class CPcapSave
{	

public:
	CPcapSave();
	~CPcapSave();
public:
	bool Init(std::string strSaveFilePath ,std::string strFileName ,int iSaveCount);
	bool InitWithThread(std::string strSaveFilePath ,std::string strFileName ); //Ê±¼ä¼ä¸ô
	bool SavePcap(char *  pData ,int iLen);
	bool SavePcapForThread(char * pData ,int iLen);
	void fflushData();
	void Unit();
	
public:
	std::string m_strPath ;
	std::string m_strFileName;
	int  m_iSeparatedCount;
	int  m_iCurrentPkg;
	ofstream *  m_pObjWriteStream ;
	
	
public:
	bool                m_isFirstWrite ;
	bool 							  m_isTHDEndFlag;
	pthread_mutex_t     m_objMutex ;
	pthread_t					  m_iThreadID ;
	static void* WorkThreadFun(void* arg);
	
	
	
};






#endif






























