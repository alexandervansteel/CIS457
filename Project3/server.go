package main

import (
	"bytes"
	"container/list"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"fmt"
	"net"
	"os"
	"strings"

	"./mycrypto"
)

type ClientChat struct {
	Name      string      // name of user
	key       string      // client key
	IN        chan string // input channel for to send to user
	OUT       chan string // input channel from user to all
	Con       net.Conn    // connection of client
	Quit      chan bool   // quit channel for all goroutines
	ListChain *list.List  // reference to list
}

// read from connection and return true if ok
func (c *ClientChat) Read(buf []byte) bool {
	_, err := c.Con.Read(buf)
	if err != nil {
		c.Close()
		return false
	}
	return true
}

// close the connection and send quit to sender
func (c *ClientChat) Close() {
	c.Quit <- true
	c.Con.Close()
	c.deleteFromList()
}

// compare two clients: name and network connection
func (c *ClientChat) Equal(cl *ClientChat) bool {
	if bytes.Equal([]byte(c.Name), []byte(cl.Name)) {
		if c.Con == cl.Con {
			return true
		}
	}
	return false
}

// delete the client from list
func (c *ClientChat) deleteFromList() {
	for e := c.ListChain.Front(); e != nil; e = e.Next() {
		client := e.Value.(ClientChat)
		if c.Equal(&client) {
			c.ListChain.Remove(e)
		}
	}
}

// handlingINOUT(): handle inputs from client, and send it to all other client via channels.
func handlingINOUT(IN <-chan string, lst *list.List) {
	for {
		input := <-IN // input, get from client

		// checks to see if the message is a pm, and then sends it to the specified person
		broadcast := true
		fmt.Println(strings.TrimSpace(input)) // prints everything server side for debug
		for val := lst.Front(); val != nil; val = val.Next() {
			client := val.Value.(ClientChat)
			if strings.Contains(input, "/"+client.Name) {
				client.IN <- input
				broadcast = false
			}

			// removes specified client(s) from server
			if strings.Contains(input, "/kick") && strings.Contains(input, "-"+client.Name) {
				client.IN <- "You have been removed from the server.\n"
				client.Close()
				broadcast = false
			}
		}

		// send to all client if the message is not a pm
		if broadcast == true {
			for val := lst.Front(); val != nil; val = val.Next() {
				client := val.Value.(ClientChat)
				if strings.Contains(input, "/who") {
					client_list := "Currently Connected > "
					for e := lst.Front(); e != nil; e = e.Next() {
						clients := e.Value.(ClientChat)
						client_list += strings.TrimSpace(clients.Name) + " > "
					}
					client.IN <- client_list
				} else {
					client.IN <- input
				}
			}
		}
	}
}

// clientreceiver wait for an input from network, after geting data it send to
// handlingINOUT via a channel.
func clientreceiver(client *ClientChat, key []byte) {
	buf := make([]byte, 2048)
	for {
		client.Read(buf)
		n := bytes.IndexByte(buf, 0)
		s := string(buf[:n])
		msg, err := mycrypto.Decrypt(key, s)
		if err != nil {
			fmt.Println("Error in clientreceiver()")
			fmt.Println(err)
			os.Exit(1)
		}
		if strings.TrimSpace(msg) == "/quit" {
			client.Close()
			break
		}
		send := strings.TrimSpace(client.Name) + "> " + msg
		client.OUT <- send
		for i := 0; i < 2048; i++ {
			buf[i] = 0x00
		}
	}
	client.OUT <- strings.TrimSpace(client.Name) + " has left chat"
}

// clientsender(): get the data from handlingINOUT via channel (or quit signal from
// clientreceiver) and send it via network
func clientsender(client *ClientChat, key []byte) {
	for {
		select {
		case buf := <-client.IN:
			msg, err := mycrypto.Encrypt(key, buf)
			if err != nil {
				fmt.Println("Error in clientsender()")
				fmt.Println(err)
				os.Exit(1)
			}
			client.Con.Write([]byte(msg))
		case <-client.Quit:
			client.Con.Close()
			break
		}
	}
}

// clientHandling(): get the username and create the clientsturct
// start the clientsender/receiver, add client to list.
func clientHandling(con net.Conn, ch chan string, lst *list.List, privkey []byte, pubkey []byte) {
	con.Write(pubkey)
	buf := make([]byte, 1024)
	con.Read(buf)
	n := bytes.IndexByte(buf, 0)
	s := string(buf[:n])
	clientkey, err := mycrypto.Decrypt(privkey, s)
	if err != nil {
		fmt.Println("Error in clientHandling() reading name.")
		fmt.Println(err)
		os.Exit(1)
	}

	key := strings.TrimSpace(clientkey)
	con.Read(buf)
	n = bytes.IndexByte(buf, 0)
	s = string(buf[:n])
	name, err := mycrypto.Decrypt([]byte(key), s)
	if err != nil {
		fmt.Println("Error in clientHandling() reading name.")
		fmt.Println(err)
		os.Exit(1)
	}

	newclient := &ClientChat{strings.TrimSpace(name), string(key), make(chan string), ch, con, make(chan bool), lst}

	go clientsender(newclient, []byte(key))
	go clientreceiver(newclient, []byte(key))
	lst.PushBack(*newclient)
	ch <- strings.TrimSpace(string(name)) + " has joined the chat"
}

func main() {
	// generate private key
	privatekey, err := rsa.GenerateKey(rand.Reader, 1024)

	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	var publickey *rsa.PublicKey
	publickey = &privatekey.PublicKey

	pubkey, err := x509.MarshalPKIXPublicKey(publickey)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	privkey := x509.MarshalPKCS1PrivateKey(privatekey)

	fmt.Println(string(privkey))
	fmt.Println(string(pubkey))

	// create the list of clients
	clientlist := list.New()
	in := make(chan string)
	go handlingINOUT(in, clientlist)

	host, _ := os.Hostname()
	addrs, _ := net.LookupIP(host)
	for _, addr := range addrs {
		if ipv4 := addr.To4(); ipv4 != nil {
			fmt.Println("IPv4: ", ipv4)
		}
	}

	// create the connection
	netlisten, _ := net.Listen("tcp", "127.0.0.1:9988")
	fmt.Println("Server Listening...")
	defer netlisten.Close()

	for {
		// wait for clients
		conn, _ := netlisten.Accept()
		go clientHandling(conn, in, clientlist, privkey, pubkey)
	}
}
