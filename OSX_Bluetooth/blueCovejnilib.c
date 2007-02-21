/**
 *  BlueCove - Java library for Bluetooth
 *  Copyright (C) 2007 Eric Wagner
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "blueCovejnilib.h"

/**
 * Called by the VM when this library is loaded. We use it to set up the CFRunLoop and sources
 */
 
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
		pthread_t				aThread;
		pthread_mutex_t			initializeMutex;
		pthread_cond_t			initializeCond;		
		
		s_inquiryList = NULL;
		s_serviceInqList = NULL;
		s_openSocketList = NULL;
		printMessage("Loading " NATIVE_DESCRIP "\n", DEBUG_INFO_LEVEL);
		
		pthread_cond_init(&initializeCond, NULL);
		pthread_mutex_init(&initializeMutex, NULL);
		s_vm = vm;
		
		pthread_mutex_lock(&initializeMutex);
		/* start the OS X thread up */
		printMessage("JNI_OnLoad: Starting the OS X init and run thread", DEBUG_INFO_LEVEL);
		pthread_create(&aThread, NULL, cocoaWrapper, (void*) &initializeCond);
		/* wait until the OS X thread has initialized before returning */
		printMessage("JNI_OnLoad: Waiting for initialization to complete", DEBUG_INFO_LEVEL);
		
		pthread_cond_wait(&initializeCond, &initializeMutex);
		
		printMessage("JNI_OnLoad: Initialization complete returning from onload", DEBUG_INFO_LEVEL);
		/* clean up*/
		pthread_cond_destroy(&initializeCond);
		pthread_mutex_unlock(&initializeMutex);
		pthread_mutex_destroy(&initializeMutex);

		printMessage("Loaded " NATIVE_DESCRIP, DEBUG_NORM_LEVEL);
		
		return JNI_VERSION_1_2;
}

	
JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved){
	/* should cleanup even be attempted? */
	printMessage("Unloading " NATIVE_DESCRIP "\n", DEBUG_INFO_LEVEL);
}



/*
 * Class:     com_intel_bluetooth_BluetoothPeer
 * Method:    doInquiry
 * Signature: (ILjavax/bluetooth/DiscoveryListener;)I
 */
JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_doInquiry
  (JNIEnv *env, jobject peer, jint accessCode, jobject listener){
		
		CFRunLoopSourceContext		aContext={0};
		doInquiryRec				*record;
		
		printMessage("Java_com_intel_bluetooth_BluetoothPeer_doInquiry called", DEBUG_INFO_LEVEL);
		
		CFRunLoopSourceGetContext(s_inquiryStartSource, &aContext);

		/* set the data for the work function */
		record = (doInquiryRec*) aContext.info;
		record->peer = (*env)->NewGlobalRef(env, peer);
		record->accessCode = accessCode;
		record->listener = (*env)->NewGlobalRef(env, listener);

		CFRunLoopSourceSignal(s_inquiryStartSource);
		CFRunLoopWakeUp (s_runLoop);
		
		printMessage("Java_com_intel_bluetooth_BluetoothPeer_doInquiry exiting", DEBUG_INFO_LEVEL);
		return -1;
	
}


JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothPeer_cancelInquiry
  (JNIEnv *env, jobject peer, jobject listener){
	
	cancelInquiryRec			*record;
	pthread_mutex_t				aMutex;
	CFRunLoopSourceContext		aContext = {0};
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_cancelInquiry: called", DEBUG_INFO_LEVEL);
	

	CFRunLoopSourceGetContext(s_inquiryStopSource, &aContext);

	record = (cancelInquiryRec*) aContext.info;
	record->peer = peer;
	record->listener = listener; /* no need for a global ref since we're done with this when we return */

	pthread_cond_init(&record->waiter, NULL);
	pthread_mutex_init(&aMutex, NULL);
	pthread_mutex_lock(&aMutex);
		/* set the data for the work function */
	
	CFRunLoopSourceSignal(s_inquiryStopSource);
	CFRunLoopWakeUp (s_runLoop);
	
	/* wait until the work is done */
	pthread_cond_wait(&record->waiter, &aMutex);
	
	/* cleanup */
	pthread_cond_destroy(&record->waiter);
	pthread_mutex_destroy(&aMutex);
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_cancelInquiry: exiting", DEBUG_INFO_LEVEL);

	
	return record->success;
  }
  
JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_asyncSearchServices
  (JNIEnv *env, jobject peer, jintArray attrSet, jobjectArray uuidSet, jobject device, jobject listener){
	
	searchServicesRec				*record;
	CFRunLoopSourceContext		aContext = {0};
	currServiceInq				*mySearchServices;
	jstring						deviceAddress;
	jmethodID					getAddress;
	jclass						deviceClass;
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_asyncSearchServices: called", DEBUG_INFO_LEVEL);
	
	deviceClass = (*env)->GetObjectClass(env, device);
	getAddress = (*env)->GetMethodID(env, deviceClass, "getBluetoothAddress", "()Ljava/lang/String;");
	deviceAddress = (*env)->CallObjectMethod(env, device, getAddress);
	
	
	mySearchServices = newServiceInqRec();
	CFRunLoopSourceGetContext(s_searchServicesStart, &aContext);

	record = (searchServicesRec*) aContext.info;
	record->peer =  (*env)->NewGlobalRef(env, peer);
	// what if attrSet is NULL?
	record->attrSet =  (*env)->NewGlobalRef(env, attrSet);
	record->uuidSet = (*env)->NewGlobalRef(env, uuidSet);
	record->deviceAddress = (*env)->NewGlobalRef(env, deviceAddress);
	record->device = (*env)->NewGlobalRef(env, device);
	record->listener = (*env)->NewGlobalRef(env, listener); /* no need for a global ref since we're done with this when we return */
	record->theInq = mySearchServices;
	
	CFRunLoopSourceSignal(s_searchServicesStart);
	CFRunLoopWakeUp(s_runLoop);
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_asyncSearchServices exiting", DEBUG_INFO_LEVEL);

	return mySearchServices->index;
  
}
JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_ServiceRecordImpl_native_1populateRecord
  (JNIEnv *env, jobject peer, jintArray attrIDs){
  		CFRunLoopSourceContext		aContext={0};
		populateAttributesRec		*record;
		pthread_mutex_t				aMutex;
		
		printMessage("Java_com_intel_bluetooth_ServiceRecordImpl_native_1populateRecord called", DEBUG_INFO_LEVEL);
			
		CFRunLoopSourceGetContext(s_populateServiceAttrs, &aContext);
		record = (populateAttributesRec*) aContext.info;
		record->serviceRecord = peer;
		record->attrSet = attrIDs;
		
		pthread_cond_init(&record->waiter, NULL);
		record->waiterValid = 1;
		pthread_mutex_init(&aMutex, NULL);
		pthread_mutex_lock(&aMutex);
		
		CFRunLoopSourceSignal(s_populateServiceAttrs);
		CFRunLoopWakeUp (s_runLoop);
		
		// wait until the work is done
		pthread_cond_wait(&record->waiter, &aMutex);
		
		// cleanup
		pthread_cond_destroy(&record->waiter);
		pthread_mutex_destroy(&aMutex);
	
 		printMessage("Java_com_intel_bluetooth_ServiceRecordImpl_native_1populateRecord exiting", DEBUG_INFO_LEVEL);
 
		return record->result;
  }
  
JNIEXPORT jboolean JNICALL Java_com_intel_bluetooth_BluetoothPeer_asyncStopSearchServices
  (JNIEnv *env, jobject peer, jint transID){
  	printMessage("Java_com_intel_bluetooth_BluetoothPeer_asyncStopSearchServices called", DEBUG_INFO_LEVEL);
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_asyncStopSearchServices exiting", DEBUG_INFO_LEVEL);
	
	return 0;
  
}


JNIEXPORT jintArray JNICALL Java_com_intel_bluetooth_BluetoothPeer_getServiceHandles
  (JNIEnv *env, jobject peer, jobjectArray uuidSet, jlong address){
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_getServiceHandles: called", DEBUG_INFO_LEVEL);
	
	throwException(env, "com/intel/bluetooth/NotImplementedError", "getServiceHandles not implemented on Mac OS X");

	printMessage("Java_com_intel_bluetooth_BluetoothPeer_getServiceHandles: exiting", DEBUG_INFO_LEVEL);
	
	return NULL;
  
  
  }


JNIEXPORT jbyteArray JNICALL Java_com_intel_bluetooth_BluetoothPeer_getServiceAttributes
  (JNIEnv *env, jobject peer, jintArray attrIDs, jlong address, jint handle){
    
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_getServiceAttributes called", DEBUG_INFO_LEVEL);
	
	throwException(env, "com/intel/bluetooth/NotImplementedError", "Function not implemented on Mac OS X");
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_getServiceAttributes exiting", DEBUG_INFO_LEVEL);

	return NULL;
  
  }
  

JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_registerService
  (JNIEnv *env, jobject peer, jbyteArray record){
  printMessage("Java_com_intel_bluetooth_BluetoothPeer_registerService called", DEBUG_INFO_LEVEL);
  printMessage("Java_com_intel_bluetooth_BluetoothPeer_registerService exiting", DEBUG_INFO_LEVEL);
  return 0;
  }


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_unregisterService
  (JNIEnv *env, jobject peer, jint handle){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_unregisterService called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_unregisterService exiting", DEBUG_INFO_LEVEL);
}


JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_socket
  (JNIEnv *env, jobject peer, jboolean authenticate, jboolean encrypt){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_socket called", DEBUG_INFO_LEVEL);
	
	macSocket*			aSocket;
	
	aSocket = newMacSocket();
	aSocket->encrypted = encrypt;
	aSocket->authenticate = authenticate;
		
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_socket exiting", DEBUG_INFO_LEVEL);

	return aSocket->index;
}


JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothPeer_getsockaddress
  (JNIEnv *env, jobject peer, jint socket){
    
	jstring				localAddress, propertyName;
	jclass				propList;
	jmethodID			getProperty;
	const char			*addressString;
	UInt8				addressValue[6];
	int					i;
	UInt64				intAddress;
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_getsockaddress called", DEBUG_INFO_LEVEL);
	/* I assume this always just for the local device */
	propertyName = JAVA_ENV_CHECK((*env)->NewStringUTF(env, BLUECOVE_SYSTEM_PROP_LOCAL_ADDRESS));
	propList = JAVA_ENV_CHECK((*env)->GetObjectClass(env, s_systemProperties));
	getProperty =  JAVA_ENV_CHECK((*env)->GetMethodID(env, propList, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;"));
	localAddress = JAVA_ENV_CHECK((*env)->CallObjectMethod(env, s_systemProperties, getProperty, propertyName));
	addressString =  JAVA_ENV_CHECK((*env)->GetStringUTFChars(env, localAddress, NULL));
	sscanf(addressString, "%2x-%2x-%2x-%2x-%2x-%2x", &addressValue[0], &addressValue[1], &addressValue[2],
									&addressValue[3], &addressValue[4], &addressValue[5]);
	JAVA_ENV_CHECK((*env)->ReleaseStringUTFChars(env, localAddress, addressString));
	intAddress = 0LL;
	for(i=0;i<6;i++) {
		intAddress <<= 8;
		intAddress |= addressValue[i];
	}
		
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getsockaddress exiting", DEBUG_INFO_LEVEL);

	return intAddress;
  }


JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_getsockchannel
  (JNIEnv *env, jobject peer, jint socket){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getsockchannel called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getsockchannel exiting", DEBUG_INFO_LEVEL);

  
  return 0;
  }


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_connect
  (JNIEnv *env, jobject peer, jint socket, jlong address, jint channel){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_connect called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_connect exiting", DEBUG_INFO_LEVEL);
}


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_listen
  (JNIEnv *env, jobject peer, jint socket){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_listen called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_listen exiting", DEBUG_INFO_LEVEL);
}


JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_accept
  (JNIEnv *env, jobject peer, jint socket){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_accept called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_accept exiting", DEBUG_INFO_LEVEL);

  return 0;
  }


JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_recv__I
  (JNIEnv *env, jobject peer, jint socket){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_recv__I called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_recv__I exiting", DEBUG_INFO_LEVEL);

  return 0;
  }


JNIEXPORT jint JNICALL Java_com_intel_bluetooth_BluetoothPeer_recv__I_3BII
  (JNIEnv *env, jobject peer, jint socket, jbyteArray b, jint off, jint len){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_recv__I_3BII called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_recv__I_3BII exiting", DEBUG_INFO_LEVEL);

  return 0;
  }


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_send__II
  (JNIEnv *env, jobject peer, jint socket, jint b){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_send__II called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_send__II exiting", DEBUG_INFO_LEVEL);
}


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_send__I_3BII
  (JNIEnv *env, jobject peer, jint socket, jbyteArray b, jint off, jint len){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_send__I_3BII called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_send__I_3BII exiting", DEBUG_INFO_LEVEL);

  }


JNIEXPORT void JNICALL Java_com_intel_bluetooth_BluetoothPeer_close
  (JNIEnv *env, jobject peer, jint socket){
  
	macSocket				*aSocket;
	
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_close called", DEBUG_INFO_LEVEL);
    
	aSocket = getMacSocket(socket);
	if(aSocket->l2capRef) {

		/* TODO call IOBluetoothL2CAPChannelCloseChannel in the OS X thread */
	}
	if(aSocket->rfcommRef) {
	
		/* TODO call IOBluetoothRFCOMMChannelCloseChannel */
	}
	
	disposeMacSocket(aSocket);
	
	
	printMessage("Java_com_intel_bluetooth_BluetoothPeer_close exiting", DEBUG_INFO_LEVEL);

  
  }


JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothPeer_getpeername
  (JNIEnv *env, jobject peer, jlong address){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getpeername called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getpeername exiting", DEBUG_INFO_LEVEL);

  		return (*env)->NewStringUTF(env, "fixme");
 }


JNIEXPORT jlong JNICALL Java_com_intel_bluetooth_BluetoothPeer_getpeeraddress
  (JNIEnv *env, jobject peer, jint socket) {
  
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getpeeraddress called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getpeeraddress exiting", DEBUG_INFO_LEVEL);

	return 0LL;
	}


JNIEXPORT jstring JNICALL Java_com_intel_bluetooth_BluetoothPeer_getradioname
  (JNIEnv *env, jobject peer, jlong address){
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getradioname called", DEBUG_INFO_LEVEL);
    printMessage("Java_com_intel_bluetooth_BluetoothPeer_getradioname exiting", DEBUG_INFO_LEVEL);

		return (*env)->NewStringUTF(env, "fixme");
  
  }

JNIEXPORT jobject JNICALL Java_com_intel_bluetooth_BluetoothPeer_getAdjustedSystemProperties
  (JNIEnv *env, jobject peer){
	
	return s_systemProperties;
  
  }
