#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define NUM_PRODUCTS 20
#define NUM_CUSTOMERS 5
#define NUM_ORDERS_PER_CUSTOMER 10
#define PORT 8080

typedef struct {
    char description[50];
    float price;
    int item_count;
    int request_count;
    int sold_count;
    char unsatisfied_users[100][50];
    int unsatisfied_count;
    pthread_mutex_t lock;
} Product;

Product *catalog;
int shm_id;

void initialize_catalog() {
    for (int i = 0; i < NUM_PRODUCTS; i++) {
        snprintf(catalog[i].description, sizeof(catalog[i].description), "Product_%d", i + 1);
        catalog[i].price = (float)((i + 1) * 10);
        catalog[i].item_count = 2;
        catalog[i].request_count = 0;
        catalog[i].sold_count = 0;
        catalog[i].unsatisfied_count = 0;
        pthread_mutex_init(&catalog[i].lock, NULL);
    }
}

void process_order(int product_index, int customer_id, char *response) {
    if (product_index < 0 || product_index >= NUM_PRODUCTS) {
        snprintf(response, 100, "Invalid product index\n");
        return;
    }

    Product *product = &catalog[product_index];
    pthread_mutex_lock(&product->lock);

    product->request_count++;
    if (product->item_count > 0) {
        product->item_count--;
        product->sold_count++;
        snprintf(response, 100, "Order successful: %s, Price=%.2f\n", product->description, product->price);
    } else {
        snprintf(response, 100, "Order failed: %s is out of stock\n", product->description);
        snprintf(product->unsatisfied_users[product->unsatisfied_count], 50, "Customer_%d", customer_id);
        product->unsatisfied_count++;
    }
    pthread_mutex_unlock(&product->lock);
}

void handle_client(int client_sock, int customer_id) {
    for (int j = 0; j < NUM_ORDERS_PER_CUSTOMER; j++) {
        int product_index;
        read(client_sock, &product_index, sizeof(product_index));
        char response[100];
        process_order(product_index, customer_id, response);
        write(client_sock, response, sizeof(response));
    }
    close(client_sock);
}

void print_statistics() {
    int total_requests = 0, successful_orders = 0, failed_orders = 0;
    float total_revenue = 0.0;
    
    printf("\n=== Product Statistics ===\n");
    for (int i = 0; i < NUM_PRODUCTS; i++) {
        printf("Product: %s\nRequests: %d\nSold: %d\nUnsatisfied Users: ",
               catalog[i].description, catalog[i].request_count, catalog[i].sold_count);
        if (catalog[i].unsatisfied_count == 0) printf("None\n");
        else {
            for (int j = 0; j < catalog[i].unsatisfied_count; j++) {
                printf("%s ", catalog[i].unsatisfied_users[j]);
            }
            printf("\n");
        }
        printf("--------------------------\n");
        total_requests += catalog[i].request_count;
        successful_orders += catalog[i].sold_count;
        failed_orders += catalog[i].unsatisfied_count;
        total_revenue += catalog[i].sold_count * catalog[i].price;
    }
    printf("\n=== Overall Statistics ===\n");
    printf("Total Requests: %d\nSuccessful Orders: %d\nFailed Orders: %d\nTotal Revenue: %.2f\n", total_requests, successful_orders, failed_orders, total_revenue);
}

void start_server() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, NUM_CUSTOMERS);

    initialize_catalog();

    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (fork() == 0) {
            close(server_sock);
            handle_client(client_sock, i + 1);
            exit(0);
        }
        close(client_sock);
    }
    for (int i = 0; i < NUM_CUSTOMERS; i++) wait(NULL);
    print_statistics();
    close(server_sock);
    shmctl(shm_id, IPC_RMID, NULL);
}

void client_process(int customer_id) {
    pid_t pid = fork();
    if (pid == 0) {  // Child process
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in server_addr = {AF_INET, htons(PORT), inet_addr("127.0.0.1")};

        connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
        srand(time(NULL) ^ getpid());
        
        for (int j = 0; j < NUM_ORDERS_PER_CUSTOMER; j++) {
            int product_index = rand() % NUM_PRODUCTS;
            write(sock, &product_index, sizeof(product_index));
            char response[100];
            read(sock, response, sizeof(response));
            printf("Customer_%d received: %s", customer_id, response);
            sleep(1);
        }
        
        close(sock);
        exit(0);  // Child process exits after completing orders
    }
}

int main() {
    shm_id = shmget(IPC_PRIVATE, sizeof(Product) * NUM_PRODUCTS, IPC_CREAT | 0666);
    catalog = (Product*)shmat(shm_id, NULL, 0);

    if (fork() == 0) { start_server(); exit(0); }
    sleep(1);
    for (int i = 0; i < NUM_CUSTOMERS; i++) {
        client_process(i + 1);
    }
    for (int i = 0; i < NUM_CUSTOMERS; i++) wait(NULL);
    shmdt(catalog);
    return 0;
}

