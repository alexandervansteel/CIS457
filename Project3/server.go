package main

import (
	"bytes"
	"container/list"
	"fmt"
	"net"
	"strings"
)

type ClientChat struct {
	Name      string      // name of user
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
		fmt.Println(input) // prints everything server side for debug
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
          for e:= lst.Front(); e != nil; e = e.Next() {
            clients := e.Value.(ClientChat)
            client_list += clients.Name + " > "
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
func clientreceiver(client *ClientChat) {
	buf := make([]byte, 2048)
	for client.Read(buf) {
		if bytes.Equal(buf, []byte("/quit")) {
			client.Close()
			break
		}
		send := client.Name + "> " + string(buf)
		client.OUT <- send
		for i := 0; i < 2048; i++ {
			buf[i] = 0x00
		}
	}
	client.OUT <- client.Name + " has left chat"
}

// clientsender(): get the data from handlingINOUT via channel (or quit signal from
// clientreceiver) and send it via network
func clientsender(client *ClientChat) {
	for {
		select {
		case buf := <-client.IN:
			client.Con.Write([]byte(buf))
		case <-client.Quit:
			client.Con.Close()
			break
		}
	}
}

// clientHandling(): get the username and create the clientsturct
// start the clientsender/receiver, add client to list.
func clientHandling(con net.Conn, ch chan string, lst *list.List) {
	buf := make([]byte, 1024)
	bytenum, _ := con.Read(buf)
	name := string(buf[0:bytenum])
	newclient := &ClientChat{name, make(chan string), ch, con, make(chan bool), lst}

	go clientsender(newclient)
	go clientreceiver(newclient)
	lst.PushBack(*newclient)
	ch <- name + " has joinet the chat"
}

func main() {

	// create the list of clients
	clientlist := list.New()
	in := make(chan string)
	go handlingINOUT(in, clientlist)

	// create the connection
	netlisten, _ := net.Listen("tcp", "127.0.0.1:9988")
  fmt.Println("Server Listening...")
	defer netlisten.Close()

	for {
		// wait for clients
		conn, _ := netlisten.Accept()
		go clientHandling(conn, in, clientlist)
	}
}
