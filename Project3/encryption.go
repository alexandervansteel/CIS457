package encryption

import (
	"crypto/aes"
	"crypto/cipher"
  "crypto/hmac"
  "crypto/sha256"
  "crypto/rand"

)



//Pad the remaining bytes with a byte containing the number of bytes of padding
func pad(in []byte) []byte {
	padding := 16 - (len(in) % 16)
	for i := 0; i < padding; i++ {
		in = append(in, byte(padding))
	}
	return in
}

// Unpad the slice
func unpad(in []byte) []byte {
	if len(in) == 0 {
		return nil
	}

	padding := in[len(in)-1]
	if int(padding) > len(in) || padding > aes.BlockSize {
		return nil
	} else if padding == 0 {
		return nil
	}

	for i := len(in) - 1; i > len(in)-int(padding)-1; i-- {
		if in[i] != padding {
			return nil
		}
	}
	return in[:len(in)-int(padding)]
}

func Encrypt(key, message []byte) ([]byte, error) {
	if len(key) != KeySize { //FIXME KeySize
		return nil, ErrEncrypt
	}

	iv, err := RandBytes(NonceSize) //FIXME RandBytes , NonceSize
	if err != nil {
		return nil, ErrEncrypt //FIXME ErrEncrypt
	}

	pmessage := pad(message)
	ct := make([]byte, len(pmessage))

	// NewCipher only returns an error with an invalid key size,
	// but the key size was checked at the beginning of the function.
	c, _ := aes.NewCipher(key[:CKeySize]) //FIXME CKeySize
	ctr := cipher.NewCBCEncrypter(c, iv)
	ctr.CryptBlocks(ct, pmessage)

	h := hmac.New(sha256.New, key[CKeySize:]) //FIXME CKeySize
	ct = append(iv, ct...)
	h.Write(ct)
	ct = h.Sum(ct)
	return ct, nil
}

func Decrypt(key, message []byte) ([]byte, error) {
	if len(key) != KeySize {  //FIXME KeySize
		return nil, ErrEncrypt
	}

	// HMAC-SHA-256 returns a MAC that is also a multiple of the
	// block size.
	if (len(message) % aes.BlockSize) != 0 {
		return nil, ErrDecrypt
	}

	// A message must have at least an IV block, a message block,
	// and two blocks of HMAC.
	if len(message) < (4 * aes.BlockSize) {
		return nil, ErrDecrypt
	}

	macStart := len(message) - MACSize
	tag := message[macStart:]
	out := make([]byte, macStart-NonceSize)  //FIXME NonceSize
	message = message[:macStart]

	h := hmac.New(sha256.New, key[CKeySize:])  //FIXME CKeySize
	h.Write(message)
	mac := h.Sum(nil)
	if !hmac.Equal(mac, tag) {
		return nil, ErrDecrypt
	}
	// NewCipher only returns an error with an invalid key size,
	// but the key size was checked at the beginning of the function.
	c, _ := aes.NewCipher(key[:CKeySize])  //FIXME CKeySize
	ctr := cipher.NewCBCDecrypter(c, message[:NonceSize])  //FIXME NonceSize
	ctr.CryptBlocks(out, message[NonceSize:]) //FIXME NonceSize

	pt := unpad(out)
	if pt == nil {
		return nil, ErrDecrypt
	}

	return pt, nil
}
