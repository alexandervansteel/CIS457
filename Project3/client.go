package main

import (
	"bufio"
	"bytes"
	"crypto/rand"
	// "crypto/rand"
	"fmt"
	"net"
	"os"
	"strings"
	"time"

	"./mycrypto"
)

const (
	keySizeError string = "Key size error" // Key size error message.
)

var running bool // global variable if client is running

// read from connection and return true if ok
func Read(con net.Conn, key []byte) string {
	buf := make([]byte, 2048)
	_, err := con.Read(buf)
	if err != nil {
		con.Close()
		running = false
		return "Connection Terminated"
	}
	n := bytes.IndexByte(buf, 0)
	s := string(buf[:n])
	msg, err := mycrypto.Decrypt(key, s)
	if err != nil {
		fmt.Println("Error in Read()")
		fmt.Println(err)
		os.Exit(1)
	}

	err_message := "You have been removed from the server.\n"
	if strings.Compare(string(msg), err_message) == 0 {
		con.Close()
		running = false
		return msg
	}

	return msg
}

// clientsender(): read from stdin and send it via network
func clientsender(cn net.Conn, name []byte, key []byte) {
	reader := bufio.NewReader(os.Stdin)
	for {
		//fmt.Print(name)
		input, _ := reader.ReadString('\n')
		if strings.TrimSpace(input) == "/quit" {
			msg, err := mycrypto.Encrypt(key, input)
			if err != nil {
				fmt.Println("Error in clientsender() for /quit")
				fmt.Println(err)
				os.Exit(1)
			}
			cn.Write([]byte(msg))
			cn.Close()
			running = false
			break
		}
		//cn.Write([]byte(input[0 : len(input)-1]))
		msg, err := mycrypto.Encrypt(key, input)
		if err != nil {
			fmt.Println("Error in clientsender() for regular message")
			fmt.Println(err)
			os.Exit(1)
		}
		cn.Write([]byte(msg))
	}
}

// clientreceiver(): wait for input from network and print it out
func clientreceiver(cn net.Conn, key []byte) {
	for running {
		fmt.Println(strings.TrimSpace(Read(cn, key)))
	}
}

func GenSymmetricKey(bits int) (k []byte, err error) {
	if bits <= 0 || bits%8 != 0 {
		return nil, fmt.Errorf(keySizeError)
	}

	size := bits / 8
	k = make([]byte, size)
	if _, err = rand.Read(k); err != nil {
		return nil, err
	}

	return k, nil
}

func main() {
	running = true

	symkey := []byte("thisisatempkeyfortesting")

	// connect
	reader := bufio.NewReader(os.Stdin)
	fmt.Println("Enter the IP address: ")
	ip, err := reader.ReadString('\n')
	if err != nil {
		fmt.Println("Error reading IP Address", err)
		os.Exit(1)
	}
	fmt.Println("Enter the port number: ")
	port, err := reader.ReadString('\n')
	if err != nil {
		fmt.Println("Error reading port number", err)
		os.Exit(1)
	}
	dest := strings.TrimSpace(ip) + ":" + strings.TrimSpace(port)
	fmt.Println(dest)
	//dest:= "127.0.0.1:9988"
	cn, _ := net.Dial("tcp", dest)
	defer cn.Close()
	/* key stuff removed for testing
	// load public key
	buf := make([]byte, 4096)
	//cn.Read(buf)
	n, err := cn.Read(buf)
	if err != nil {
		fmt.Println("Error in client main() for receive key")
		fmt.Println(err)
		os.Exit(1)
	}
	s := string(buf[:n])
	s = strings.TrimSpace(s)
	fmt.Println(s)
	pubkey := []byte(s)
	//generate symmetric key
	symkey, err := GenSymmetricKey(1024)
	if err != nil {
		fmt.Println("Error in client main() for generate key")
		fmt.Println(err)
		os.Exit(1)
	}

	//strkey := x509.MarshalPKCS1PrivateKey(symkey)

	msg, err := mycrypto.Encrypt(pubkey, string(symkey))
	if err != nil {
		fmt.Println("Error in client main() for encrypt key")
		fmt.Println(err)
		os.Exit(1)
	}

	cn.Write([]byte(msg))
	*/
	// get the user name
	fmt.Print("Please give your name: ")
	name, _ := reader.ReadString('\n')
	encrypted_name, err := mycrypto.Encrypt(symkey, name)
	if err != nil {
		fmt.Println("Error with name encryption")
		fmt.Println(err)
		os.Exit(1)
	}
	cn.Write([]byte(encrypted_name))

	// start receiver and sender
	go clientreceiver(cn, symkey)
	go clientsender(cn, []byte(name), symkey)

	// wait for quiting (/quit). run until running is true
	for running {
		time.Sleep(1 * 1e9)
	}
}
