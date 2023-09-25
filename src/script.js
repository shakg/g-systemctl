// Define a variable to store the fetched data
let serviceData = [];

// Function to render the data on the web page
function renderData(data) {
    const serviceList = document.getElementById("service-list");
    serviceList.innerHTML = "";

    data.forEach(service => {
        const li = document.createElement("li");
        console.log(service)
        li.textContent = `${service.unit} - ${service.sub}`;
        li.style.margin = '2px';
        if(service.sub === "running"){
            li.style.backgroundColor = 'green';
            li.style.color = 'white'
        }
        serviceList.appendChild(li);
    });
}

// Fetch data from the server and render it initially
async function fetchData() {
    try {
        // Use the fetch API to make a GET request to your server endpoint
        const response = await fetch('/api'); // Replace '/api' with your actual server endpoint

        // Check if the response status is OK (HTTP 200)
        if (!response.ok) {
            throw new Error(`Failed to fetch data: ${response.status} ${response.statusText}`);
        }

        // Parse the response as JSON
        const data = await response.json();

        // Store the fetched data in the serviceData variable
        serviceData = data;

        // Render the fetched data
        renderData(serviceData);
    } catch (error) {
        console.error('Error fetching data:', error);
    }
}

// Filter data based on user input
function filterData() {
    const filterInput = document.getElementById("filter");
    const filterValue = filterInput.value.toLowerCase();

    const filteredData = serviceData.filter(service =>
        service.unit.toLowerCase().includes(filterValue) ||
        service.description.toLowerCase().includes(filterValue)
    );

    renderData(filteredData);
}

// Initial fetch and add event listener for filtering
document.addEventListener("DOMContentLoaded", () => {
    fetchData();

    const filterInput = document.getElementById("filter");
    filterInput.addEventListener("input", filterData);
});
