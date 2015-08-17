#ifndef ERROR_H
#define ERROR_H

enum error {
	ERR_SUCCESS = 0,
	ERR_SYSTEM,
	ERR_PATH_INVALID,
	ERR_PATH_NOT_EMPTY,
	ERR_NO_PERMISSION,
	ERR_FORMAT_INVALID,
	ERR_CONN_FAILURE,
	ERR_NO_KEY,

	ERR_CRYP_LIBCRYPTO,
	ERR_CRYP_B64_INVALID,

	ERR_PEER_DUPLICATE,
	ERR_PEER_MAX_DEPTH,

	ERR_NET_ADDR_INVALID
};


#endif
