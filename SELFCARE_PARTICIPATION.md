# Self-care participation directory
For those who would like to register to participate in the Self-Care portal Beta, you 
will be required to provide the following:
- A URI to the program within your organization
- Organization name
- URI to your Subscriber Self-care portal, including the port
- A logo of your company to be used in the rebranding of the application. This should be an JPG or PNG file of the 30px height, width 30px to 100px
- A logo of your company to be used in the directory itself. The format should be JPG or PNG and the height of 75px and width of 75px to 250 px.
- A URI to an access policy for your site
- A password policy RegEx and textual description.

## The Directory
The directory will be maintained by TIP and the above assets must be handed in for acceptance in the program.

## Location
The URI of the directory is located here:

https://s3.us-west-2.amazonaws.com/ucentral.arilia.com/tip_selfcare_registry.json

### The file format
The file actually contains a JSOn document containing an array of all the entries in the program. Here
is a sample

```json
{
  [
    {
      "org_uri" : "https://www.example.com" ,
      "org_name" : "Example Corp Inc.",
      "subscriber_portal" : "https://ucentral.dpaas.arilia.com:16006",
      "small_org_logo" : "https://www.example.com/logos/small_logo.svg",
      "large_org_logo" : "https://www.example.com/logos/big_logo.svg",
      "org_color_1" : "#3399ff",
      "access_policy" : "https://www.example.com/policies/selfcare_program_policy.html",
      "password_policy_regex" : "^(?=.*?[A-Z])(?=.*?[a-z])(?=.*?[0-9])(?=.*?[#?!@$%^&*-]).{8,}$",
      "password_policy" : "https://www.example.com/policies/selfcare_password_policy.html",
      "operator_id" : "oper1"
    }
    ...    
  ]
}
```

#### `org_uri`
The URI the subscriber can use to visit a website describing the operator

#### `org_name`
The legal name of the operator.

#### `subscriber_portal`
The location of the subscriber self-care service

#### `small_org_logo`
URL of a small logo to be used for branding the mobile app.

#### `large_org_logo`
URL of a larger logo to be used for branding the mobile app.

#### `org_color_1`
A color to be used for branding

#### `access_policy`
URL of the system access policy

#### `password_policy_regex`
The regex of the password requirement

#### `password_policy`
URL of the description of what the password policy is

#### `operator_id`
A single word to link the registration with an existing Operator in the Operator DB.

### The SelfCare application
The application is available on AppCenter by following this link.






